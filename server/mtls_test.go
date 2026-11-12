package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"math/big"
	"net"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"testing"
	"time"
)

type testCA struct {
	cert *x509.Certificate
	key  *ecdsa.PrivateKey
}

func newCA(t *testing.T, cn string) testCA {
	t.Helper()
	k, _ := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	tpl := &x509.Certificate{SerialNumber: big.NewInt(1), Subject: pkix.Name{CommonName: cn},
		NotBefore: time.Now().Add(-time.Hour), NotAfter: time.Now().AddDate(20, 0, 0), IsCA: true,
		BasicConstraintsValid: true, KeyUsage: x509.KeyUsageCertSign}
	der, err := x509.CreateCertificate(rand.Reader, tpl, tpl, &k.PublicKey, k)
	if err != nil {
		t.Fatal(err)
	}
	c, _ := x509.ParseCertificate(der)
	return testCA{c, k}
}

// issue: a leaf of the CA (a server's with the name, else a device's with the ID as CN), notAfter as given
func (ca testCA) issue(t *testing.T, cn string, server bool, notAfter time.Time) tls.Certificate {
	t.Helper()
	k, _ := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	tpl := &x509.Certificate{SerialNumber: big.NewInt(time.Now().UnixNano()), Subject: pkix.Name{CommonName: cn},
		NotBefore: time.Now().Add(-time.Hour), NotAfter: notAfter, KeyUsage: x509.KeyUsageDigitalSignature,
		ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth}}
	if server {
		tpl.ExtKeyUsage = []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}
		tpl.DNSNames, tpl.IPAddresses = []string{cn}, []net.IP{net.IPv4(127, 0, 0, 1)}
	}
	der, err := x509.CreateCertificate(rand.Reader, tpl, ca.cert, &k.PublicKey, ca.key)
	if err != nil {
		t.Fatal(err)
	}
	return tls.Certificate{Certificate: [][]byte{der}, PrivateKey: k}
}

// devServer: devRoutes behind devTLS, from files as the Pi has them
func devServer(t *testing.T, ca testCA) *httptest.Server {
	t.Helper()
	testServer(t) // the state and temporary files
	dir := t.TempDir()
	srv := ca.issue(t, "leakcam-server", true, time.Now().AddDate(20, 0, 0))
	write := func(name, typ string, b []byte) {
		if err := os.WriteFile(dir+"/"+name, pem.EncodeToMemory(&pem.Block{Type: typ, Bytes: b}), 0o600); err != nil {
			t.Fatal(err)
		}
	}
	write("server.crt", "CERTIFICATE", srv.Certificate[0])
	kb, _ := x509.MarshalECPrivateKey(srv.PrivateKey.(*ecdsa.PrivateKey))
	write("server.key", "EC PRIVATE KEY", kb)
	write("ca.crt", "CERTIFICATE", ca.cert.Raw)
	cfg, err := devTLS(dir)
	if err != nil {
		t.Fatal(err)
	}
	s := httptest.NewUnstartedServer(withTime(devRoutes()))
	s.TLS = cfg
	s.StartTLS()
	t.Cleanup(s.Close)
	return s
}

func devClient(ca testCA, cert *tls.Certificate) *http.Client {
	pool := x509.NewCertPool()
	pool.AddCert(ca.cert)
	cfg := &tls.Config{RootCAs: pool, ServerName: "leakcam-server"}
	if cert != nil {
		cfg.Certificates = []tls.Certificate{*cert}
	}
	return &http.Client{Transport: &http.Transport{TLSClientConfig: cfg}, Timeout: 5 * time.Second}
}

// the boards' port: only a device certificate of the CA, only for its own ID (DESIGN.md 5.3.12, SECURITY.txt 4)
func TestDevicesOverMTLS(t *testing.T) {
	ca := newCA(t, "LEAKCAM CA")
	s := devServer(t, ca)
	mine := ca.issue(t, TestID, false, time.Now().AddDate(20, 0, 0))
	form := url.Values{"full": {"0.6+g1234567"}}
	r, err := devClient(ca, &mine).PostForm(s.URL+"/v1/poll?id="+TestID, form)
	if err != nil || r.StatusCode != http.StatusOK || r.Header.Get("X-Time") == "" {
		t.Fatalf("own ID: %v %v", err, r)
	}
	r.Body.Close()
	if r, err = devClient(ca, &mine).PostForm(s.URL+"/v1/poll?id=000000000001", form); err != nil ||
		r.StatusCode != http.StatusForbidden {
		t.Fatalf("another ID: %v %v, want 403", err, r)
	}
	r.Body.Close()
	if _, err = devClient(ca, nil).PostForm(s.URL+"/v1/poll?id="+TestID, form); err == nil {
		t.Fatal("no client certificate: answered")
	}
	other := newCA(t, "not LEAKCAM")
	stranger := other.issue(t, TestID, false, time.Now().AddDate(20, 0, 0))
	if _, err = devClient(ca, &stranger).PostForm(s.URL+"/v1/poll?id="+TestID, form); err == nil {
		t.Fatal("a certificate of another CA: answered")
	}
	expired := ca.issue(t, TestID, false, time.Now().Add(-time.Minute))
	if _, err = devClient(ca, &expired).PostForm(s.URL+"/v1/poll?id="+TestID, form); err == nil {
		t.Fatal("an expired device certificate: answered (the server checks the dates by its own clock)")
	}
}
