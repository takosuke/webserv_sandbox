(ns webserv-tests.resilience-test
  "Resilience regression tests: a request must never hang the server, and a
  half-closed or pathological connection must not spin or stall (subject:
  'must remain operational at all times', 'a request should never hang
  indefinitely'). These probe bugs in the epoll loop, the URI decoder and CGI
  header parsing.

  Each assertion targets the *desired* behavior and is KNOWN-FAILING until the
  corresponding bug is fixed. Requests here can wedge a connection or spin the
  event loop, so each test gets its own server (:each), following
  error_response_test / parse_error_test's convention. All reads go through
  raw-request-timeout so a hanging server fails the test instead of blocking the
  whole suite."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :each (server/make-fixture "base.conf"))

;; ---------------------------------------------------------------------------
;; Half-closed / incomplete request -> epoll busy-loop
;; ---------------------------------------------------------------------------
;; A client that sends a partial request then shutdown(SHUT_WR) makes read()
;; return EOF (0). handle() never detects read()==0, so the fd stays
;; EPOLLIN-ready and the loop re-dispatches it forever at ~100% CPU while the
;; request is never completed or closed.

(deftest test-half-closed-incomplete-request-does-not-hang
  (testing "KNOWN-FAILING: an incomplete request + half-close is answered or closed, not left hanging"
    (let [{:keys [timed-out]}
          (server/raw-request-timeout "127.0.0.1" 8080
            "GET /index.html HTTP/1.0\r\nHos" 1500)]
      (is (not timed-out)
          "server should close or respond to a half-closed incomplete request"))))

(deftest test-half-close-server-stays-responsive
  (testing "Server still serves other clients after a half-closed connection"
    ;; Resilience check (not KNOWN-FAILING): trigger the half-open state on one
    ;; connection, then confirm a fresh connection is still served promptly.
    (future (server/raw-request-timeout "127.0.0.1" 8080
              "GET /index.html HTTP/1.0\r\nHos" 1000))
    (Thread/sleep 200)
    (is (server/responsive? 2000))))

;; ---------------------------------------------------------------------------
;; URI that percent-decodes to a literal '%' -> infinite decode loop
;; ---------------------------------------------------------------------------
;; %25 decodes to '%'. decode_http re-scans the result, finds the bare '%',
;; cannot decode it, and never advances the scan position -> infinite loop, the
;; request is never answered and a CPU core is pegged.

(deftest test-percent-encoded-percent-does-not-loop
  (testing "KNOWN-FAILING: a URI that decodes to a literal % is answered, not looped forever"
    (let [{:keys [response timed-out]}
          (server/raw-request-timeout "127.0.0.1" 8080
            "GET /%25 HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n" 1500)]
      (is (not timed-out) "request must terminate with a response")
      (is (re-find #"HTTP/\S+ \d{3}" response)
          "a status line should be returned"))))

(deftest test-double-encoded-percent-does-not-loop
  (testing "KNOWN-FAILING: %2525 (decodes to %25) also terminates"
    (let [{:keys [timed-out]}
          (server/raw-request-timeout "127.0.0.1" 8080
            "GET /%2525 HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n" 1500)]
      (is (not timed-out)))))

;; ---------------------------------------------------------------------------
;; CGI emitting LF-only header lines -> header parsing never completes
;; ---------------------------------------------------------------------------
;; handle_cgi_output splits CGI headers on CRLF only. A CGI using bare LF (which
;; RFC 3875 permits) never produces a recognized header terminator, so the
;; response hangs.

(deftest test-cgi-lf-only-headers-do-not-hang
  (testing "KNOWN-FAILING: a CGI emitting LF-only header lines is parsed, not hung"
    (let [{:keys [timed-out]}
          (server/raw-request-timeout "127.0.0.1" 8080
            "GET /cgi-bin/lf_headers.py HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n" 2500)]
      (is (not timed-out) "LF-terminated CGI headers must not hang the response"))))
