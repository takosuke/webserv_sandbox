(ns webserv-tests.redirect-test
  "Regression tests for `return` redirects and client_header_buffer_size, using
  redirect_buffer.conf. Each test asserts desired behavior; KNOWN-FAILING items
  encode current bugs and should turn green once fixed."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "redirect_buffer.conf"))

;; ---------------------------------------------------------------------------
;; `return` redirects
;; ---------------------------------------------------------------------------

(deftest test-external-return-redirects
  (testing "external `return 302 <url>` sends a 302 with the Location header (works today)"
    (let [resp (server/http-request
                 "GET /ext HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n")]
      (is (= 302 (:status resp)))
      (is (= "https://example.com/" (get-in resp [:headers "location"]))))))

(deftest test-internal-return-redirects
  (testing "KNOWN-FAILING: internal `return 301 /index.html` yields the redirect, not 500"
    ;; handle_setup sets _req.path from `return` but never re-resolves _loc, so
    ;; the same location's `return` fires every loop iteration until
    ;; REDIRECT_LIMIT is hit and the request collapses into a forced 500.
    (let [resp (server/http-request
                 "GET /old HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n")]
      (is (not= 500 (:status resp))
          "internal return must not collapse into a 500")
      (is (= 301 (:status resp))))))

;; ---------------------------------------------------------------------------
;; client_header_buffer_size actually resizes the buffer
;; ---------------------------------------------------------------------------
;; ScratchBuffer::set_capacity allocates a new buffer but never updates the
;; `capacity` member, so the buffer stays at the 1024 default no matter what the
;; directive says. A request line that fits in 16 KB is still rejected/dropped.

(deftest test-large-header-buffer-honored
  (testing "KNOWN-FAILING: client_header_buffer_size 16384 lets a ~3 KB request line through"
    (let [long-path (str "/" (apply str (repeat 3000 "a")))
          {:keys [response timed-out]}
          (server/raw-request-timeout "127.0.0.1" 8080
            (str "GET " long-path " HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n") 2000)
          status (when-let [[_ c] (re-find #"HTTP/\S+ (\d{3})" response)]
                   (Integer/parseInt c))]
      (is (not timed-out) "server should answer, not drop the connection")
      (is (some? status) "a status line is returned")
      ;; With the buffer honored, the 3 KB line parses fine; the target file does
      ;; not exist, so it resolves to the configured 404 error page. Today the
      ;; buffer stays at 1024, the line overflows, and the request collapses into
      ;; a 500 (and neither 414 nor 500 is the correct answer here).
      (is (= 404 status)
          (str "expected 404 for a missing path via a 16 KB header buffer, got " status)))))
