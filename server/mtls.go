package main

// The boards' side of the server over mTLS (DESIGN.md 5.3.12 design decision, SECURITY.txt 4, tests/test109): one CA
// (tools/pki/leakcam_pki.py, EC P-256, 20 years) signs the server's certificate and every device's. The BL616's
// requests - the check-in, its crash record, the OTA image - are served only here: a client without a certificate of
// that CA gets no handshake, and a device certificate speaks only for its own ID (its CN). The server checks the
// device certificate's dates with its own clock; the board ignores the dates only on its first check without a clock.

import (
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"log"
	"net/http"
	"os"
)

const (
	DevAddr = ":8443"                       // LEAKCAM_DEV_ADDR: the boards' mTLS port
	TLSDir  = "/home/pi/leakcam-server/tls" // LEAKCAM_TLS: ca.crt, server.crt, server.key (server/deploy.sh)
	TLSMin  = tls.VersionTLS12              // the BL616's mbedTLS: 1.2 (1.3 when it fits, test109 step 1)
)

// devRoutes: what a board asks for, each behind the certificate's ID check
func devRoutes() *http.ServeMux {
	m := http.NewServeMux()
	m.HandleFunc("POST /v1/poll", ownID(poll))
	m.HandleFunc("POST /v1/devices/{id}/crash", ownID(crash))
	m.HandleFunc("GET /v1/images/{name}", images) // the same image for every board: any device certificate
	return m
}

// ownID: the request's device ID (?id= or the path's {id}) must be the client certificate's CN: one board's key never
// reports for another
func ownID(h http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		id := r.PathValue("id")
		if id == "" {
			id = r.URL.Query().Get("id")
		}
		if cn := peerCN(r); cn == "" || cn != id {
			log.Printf("mtls: certificate %q asked for device %q: refused", cn, id)
			http.Error(w, "the certificate is not this device's", http.StatusForbidden)
			return
		}
		h(w, r)
	}
}

func peerCN(r *http.Request) string {
	if r.TLS == nil || len(r.TLS.PeerCertificates) == 0 {
		return ""
	}
	return r.TLS.PeerCertificates[0].Subject.CommonName
}

// devTLS: the server's certificate and the CA that every client certificate must chain to (verified by Go, dates by
// the server's clock)
func devTLS(dir string) (*tls.Config, error) {
	cert, err := tls.LoadX509KeyPair(dir+"/server.crt", dir+"/server.key")
	if err != nil {
		return nil, err
	}
	pem, err := os.ReadFile(dir + "/ca.crt")
	if err != nil {
		return nil, err
	}
	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(pem) {
		return nil, fmt.Errorf("%s/ca.crt: no certificate", dir)
	}
	return &tls.Config{Certificates: []tls.Certificate{cert}, ClientCAs: pool,
		ClientAuth: tls.RequireAndVerifyClientCert, MinVersion: TLSMin}, nil
}

// serveDevices: the mTLS listener; a missing certificate is fatal (no plain fallback for the boards)
func serveDevices(addr, dir string) {
	cfg, err := devTLS(dir)
	if err != nil {
		log.Fatalf("mtls: %v (tools/pki/leakcam_pki.py, server/deploy.sh)", err)
	}
	s := &http.Server{Addr: addr, Handler: withTime(devRoutes()), TLSConfig: cfg,
		ErrorLog: log.New(os.Stderr, "mtls: ", log.LstdFlags)}
	log.Printf("mtls: boards on %s", addr)
	log.Fatal(s.ListenAndServeTLS("", ""))
}
