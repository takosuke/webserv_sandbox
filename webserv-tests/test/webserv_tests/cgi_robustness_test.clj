(ns webserv-tests.cgi-robustness-test
  "Regression tests for CGI-handling defects found by source review (see
  Architecture.md \"Additional issues found by source review\"). These probe the
  CGI output path in ClientConnection: a blocking waitpid that stalls the whole
  event loop, EPOLLHUP truncation of large bodies, a CGI that exits before
  emitting headers (100% CPU spin), a CGI header block that overflows the
  configured buffer, case-sensitive Status matching, and a stray CRLF leaking
  into the body.

  Each assertion targets the *desired* behavior and is KNOWN-FAILING until the
  bug is fixed. Several of these requests can stall or spin the server, so —
  like resilience_test / error_response_test — each test gets its own server
  (:each) and every read is deadline-guarded via raw-request-timeout /
  responsive?."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :each (server/make-fixture "base.conf"))

;; ---------------------------------------------------------------------------
;; finalize_cgi() blocks the event loop in waitpid(pid, NULL, 0)
;; ---------------------------------------------------------------------------
;; slow_exit.py closes stdout (so the server proceeds to finalize) but stays
;; alive for 3 s. A synchronous waitpid blocks the single-threaded loop for that
;; whole time, so a concurrent client cannot be served.

(deftest test-slow-exiting-cgi-does-not-block-the-loop
  (testing "KNOWN-FAILING: a CGI that lingers after closing stdout must not stall other clients"
    (future (server/raw-request-timeout "127.0.0.1" 8080
              "GET /cgi-bin/slow_exit.py HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n" 4000))
    (Thread/sleep 400) ; let the slow CGI reach finalize_cgi/waitpid
    (is (server/responsive? 1500)
        "server should keep serving while a CGI lingers in waitpid")))

;; ---------------------------------------------------------------------------
;; EPOLLHUP truncates CGI output still buffered in the pipe
;; ---------------------------------------------------------------------------
;; big_body.py emits 100000 bytes then exits. The server fills at most ~1 KB per
;; event and finalizes immediately on HUP, dropping whatever is still in the
;; pipe, so the delivered body is shorter than the advertised Content-Length.

(deftest test-large-cgi-body-not-truncated
  (testing "KNOWN-FAILING: a large CGI body is delivered whole, not cut off at EPOLLHUP"
    (let [{:keys [status content-length body-bytes]}
          (server/http-get-bytes "/cgi-bin/big_body.py")]
      (is (= 200 status))
      (is (= 100000 content-length) "server should forward the CGI Content-Length")
      (is (= 100000 (alength body-bytes))
          (str "delivered " (alength body-bytes) " of 100000 CGI body bytes")))))

;; ---------------------------------------------------------------------------
;; CGI that exits before emitting a full header line -> 100% CPU spin
;; ---------------------------------------------------------------------------
;; no_output.py writes nothing and exits. CGI_HEADERS returns before the HUP
;; check when no "\r\n" is present, so the level-triggered EPOLLHUP re-fires
;; forever and the request is never answered or closed.

(deftest test-cgi-with-no-output-does-not-spin
  (testing "KNOWN-FAILING: a CGI that exits with no output is answered or closed, not spun on"
    (let [{:keys [timed-out]}
          (server/raw-request-timeout "127.0.0.1" 8080
            "GET /cgi-bin/no_output.py HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n" 2000)]
      (is (not timed-out)
          "an empty-output CGI must not leave the connection hanging"))))

;; ---------------------------------------------------------------------------
;; A CGI header larger than the configured buffer is rejected, not spun on
;; ---------------------------------------------------------------------------
;; big_header.py emits an ~3 KB X-Big value. buffer_res_headers only places a
;; header when fill_capacity() exceeds its size, so under base.conf's 1024-byte
;; client_header_buffer_size it can never be placed. Rather than spin on the
;; level-triggered EPOLLHUP, handle_cgi_output answers 502 — the same call nginx
;; makes ("if it exceeds the buffer size, the response is considered invalid").
;; The limit is configurable, so the other side of it — a header that does fit —
;; is covered by cgi_header_buffer_test.clj.

(deftest test-cgi-header-over-buffer-is-502-not-a-hang
  (testing "a CGI header exceeding client_header_buffer_size yields 502, not a hang"
    (let [{:keys [response timed-out]}
          (server/raw-request-timeout "127.0.0.1" 8080
            "GET /cgi-bin/big_header.py HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n" 2000)]
      (is (not timed-out) "the response must complete, not wedge")
      (is (re-find #"HTTP/\S+ 502" response)
          "an over-buffer CGI header block must be reported as a bad gateway"))))

;; ---------------------------------------------------------------------------
;; CGI Status header matched case-insensitively
;; ---------------------------------------------------------------------------
;; lc_status.py sends "status: 200 OK". The server compares key == "Status"
;; case-sensitively, so it is treated as an ordinary header and no status line
;; is emitted at all.

(deftest test-cgi-lowercase-status-honored
  (testing "KNOWN-FAILING: a lowercase CGI `status:` still produces an HTTP 200 status line"
    (let [resp (server/http-request
                 "GET /cgi-bin/lc_status.py HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")]
      (is (= 200 (:status resp))
          "the server must recognize a case-insensitive CGI Status header"))))

;; ---------------------------------------------------------------------------
;; Stray CRLF leaks into the CGI body
;; ---------------------------------------------------------------------------
;; The header loop stops at the blank line but never erases those 2 bytes, so
;; the body temp file starts with "\r\n". fixed_body.py's body begins with 'F'
;; (0x46); a correct server delivers a body whose first byte is 'F', not '\r'.

(deftest test-cgi-body-has-no-leading-crlf
  (testing "KNOWN-FAILING: the CGI body is delivered without an injected leading CRLF"
    (let [{:keys [status body-bytes]} (server/http-get-bytes "/cgi-bin/fixed_body.py")]
      (is (= 200 status))
      (is (pos? (alength body-bytes)) "a body should be present")
      (is (= 70 (aget body-bytes 0)) ; ASCII 'F'
          "body should start with the CGI's first byte, not an injected '\\r'"))))
