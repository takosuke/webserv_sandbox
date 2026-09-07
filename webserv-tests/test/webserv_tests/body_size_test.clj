(ns webserv-tests.body-size-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "body_size_limit.conf"))

(deftest test-normal-get-unaffected
  (testing "GET requests are not affected by the body size limit"
    (let [resp (server/http-get "/index.html")]
      (is (not= 413 (:status resp))))))

(defn- post-raw [body]
  (let [payload (str "POST / HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: "
                     (count body) "\r\n\r\n" body)]
    (server/status-code (server/raw-request "127.0.0.1" 8080 payload))))

(deftest test-small-body-accepted
  (testing "POST with body under client_max_body_size is not rejected with 413"
    (is (not= 413 (post-raw "small")))))

(deftest test-oversized-body-rejected
  (testing "POST with body exceeding client_max_body_size (100 bytes) returns 413"
    (is (= 413 (post-raw (apply str (repeat 200 "X")))))))

;; ---------------------------------------------------------------------------
;; The 413 must not depend on the client closing its write side.
;; ---------------------------------------------------------------------------
;; post-raw above goes through raw-request, which calls .shutdownOutput — that
;; EOF is what the server used to rely on to leave DISCARD_BODY. curl and
;; browsers keep the connection open and just wait, so a body that fitted into
;; the header buffer got no answer at all: handle_setup counted it as already
;; discarded but never compared it against Content-Length, and handle_timeout
;; then dropped the connection 60 s later having sent nothing.

(defn- post-keepalive
  "POST body without half-closing, returning [status timed-out]."
  [body]
  (let [payload (str "POST /probe.txt HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                     "Content-Type: plain/text\r\nContent-Length: "
                     (count body) "\r\n\r\n" body)
        {:keys [response timed-out]}
        (server/raw-request-keepalive "127.0.0.1" 8080 payload 5000)]
    [(when-let [[_ c] (re-find #"HTTP/\S+ (\d{3})" response)]
       (Integer/parseInt c))
     timed-out]))

(deftest test-oversized-body-answered-without-half-close
  (testing "a small over-limit body is answered while the client still holds the connection open"
    ;; 200 bytes: over the 100-byte cap, but small enough to arrive together
    ;; with the headers, which is exactly the case that used to hang.
    (let [[status timed-out] (post-keepalive (apply str (repeat 200 "X")))]
      (is (not timed-out)
          "the server must answer without waiting for the client to close")
      (is (= 413 status)))))

(deftest test-oversized-body-larger-than-buffer-without-half-close
  (testing "an over-limit body larger than the header buffer is also answered"
    ;; This one always worked: leftover bytes stay in the socket, so the drain
    ;; path re-tests the counter. Kept as the control for the test above.
    (let [[status timed-out] (post-keepalive (apply str (repeat 2000 "X")))]
      (is (not timed-out))
      (is (= 413 status)))))

(deftest test-under-limit-body-accepted-without-half-close
  (testing "a body under the cap is still accepted when the client holds the connection open"
    (let [[status timed-out] (post-keepalive (apply str (repeat 50 "X")))]
      (is (not timed-out))
      (is (contains? #{201 204} status)
          (str "expected 201/204 for an under-limit upload, got " status)))))
