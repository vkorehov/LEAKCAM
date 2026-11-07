package main

// fcm: Firebase Cloud Messaging HTTP v1, standard library only (DESIGN.md 5.3.13): the service account's key signs a
// JWT, Google's token endpoint trades it for an access token (kept until shortly before it expires), each message goes
// to one phone's FCM token with Android priority HIGH (it wakes an idle phone at once).

import (
	"bytes"
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"strings"
	"sync"
	"time"
)

const (
	FcmKeyFile = "/home/pi/leakcam-server/fcm.json" // LEAKCAM_FCM: the Firebase service account's key (mode 600)
	FcmScope   = "https://www.googleapis.com/auth/firebase.messaging"
)

// FcmKey: the fields of a service account key file that FCM needs
type FcmKey struct {
	ProjectID   string `json:"project_id"`
	ClientEmail string `json:"client_email"`
	PrivateKey  string `json:"private_key"`
	TokenURI    string `json:"token_uri"`
}

var (
	fcmKey     *FcmKey
	fcmRSA     *rsa.PrivateKey
	fcmMu      sync.Mutex
	fcmToken   string
	fcmExpires time.Time
	fcmURL     = "https://fcm.googleapis.com" // the tests point it at a fake
)

// loadFcm: the key from path; without one the server runs, and alerts are only logged
func loadFcm(path string) error {
	b, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	var k FcmKey
	if err := json.Unmarshal(b, &k); err != nil {
		return err
	}
	blk, _ := pem.Decode([]byte(k.PrivateKey))
	if blk == nil {
		return errors.New("no PEM private key")
	}
	pk, err := x509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		return err
	}
	rk, ok := pk.(*rsa.PrivateKey)
	if !ok {
		return errors.New("not an RSA key")
	}
	fcmKey, fcmRSA = &k, rk
	return nil
}

func b64(b []byte) string { return base64.RawURLEncoding.EncodeToString(b) }

// accessToken: an OAuth access token for FCM, a new one a minute before the old expires
func accessToken() (string, error) {
	fcmMu.Lock()
	defer fcmMu.Unlock()
	if fcmToken != "" && time.Until(fcmExpires) > time.Minute {
		return fcmToken, nil
	}
	now := time.Now()
	head, _ := json.Marshal(map[string]string{"alg": "RS256", "typ": "JWT"})
	claims, _ := json.Marshal(map[string]any{"iss": fcmKey.ClientEmail, "scope": FcmScope, "aud": fcmKey.TokenURI,
		"iat": now.Unix(), "exp": now.Add(time.Hour).Unix()})
	unsigned := b64(head) + "." + b64(claims)
	sum := sha256.Sum256([]byte(unsigned))
	sig, err := rsa.SignPKCS1v15(rand.Reader, fcmRSA, crypto.SHA256, sum[:])
	if err != nil {
		return "", err
	}
	resp, err := http.PostForm(fcmKey.TokenURI, url.Values{
		"grant_type": {"urn:ietf:params:oauth:grant-type:jwt-bearer"}, "assertion": {unsigned + "." + b64(sig)}})
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	var t struct {
		AccessToken string `json:"access_token"`
		ExpiresIn   int    `json:"expires_in"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&t); err != nil || t.AccessToken == "" {
		return "", fmt.Errorf("token endpoint %s: %v", resp.Status, err)
	}
	fcmToken, fcmExpires = t.AccessToken, now.Add(time.Duration(t.ExpiresIn)*time.Second)
	return fcmToken, nil
}

// fcmSend: one data message to one phone, priority HIGH; the app shows it (LeakAlert)
func fcmSend(token string, data map[string]string) error {
	if fcmKey == nil {
		return errors.New("no FCM key (LEAKCAM_FCM)")
	}
	at, err := accessToken()
	if err != nil {
		return err
	}
	body, _ := json.Marshal(map[string]any{"message": map[string]any{
		"token": token, "data": data, "android": map[string]any{"priority": "HIGH", "ttl": "3600s"}}})
	req, _ := http.NewRequest(http.MethodPost, fcmURL+"/v1/projects/"+fcmKey.ProjectID+"/messages:send",
		bytes.NewReader(body))
	req.Header.Set("Authorization", "Bearer "+at)
	req.Header.Set("Content-Type", "application/json")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		msg, _ := io.ReadAll(io.LimitReader(resp.Body, 300))
		return fmt.Errorf("FCM %s: %s", resp.Status, strings.TrimSpace(string(msg)))
	}
	return nil
}
