(ns webserv-tests.header-inheritance-test
  "config::header must be inherited Server -> Location (Gaps_and_Issues.md
  issue 8).

  handle_setup() ends with

      _timeout = _loc->get_header().timeout;

  so from that line onward a connection runs on the *location's* header
  timeout. Location::from_server() did not copy `header` (and neither did
  Location::operator=, issue 31), so every request reaching that line silently
  reverted to the built-in 60 s default no matter what the config said —
  holding a connection, an fd and a half-written body for a minute.

  timeout-test cannot catch this. A *static* POST returns from handle_setup
  before that line, so its 2 s is the value the ClientConnection ctor copied
  from the server and the regression is invisible there. A POST to a cgi_pass
  location does fall through to it, which is what this namespace exercises.

  Measured against timeout_location.conf (client_header_timeout 2):
    before the fix -> connection held 60.2 s
    after          -> closed in 2.9 s

  Note the parser accepts client_header_timeout at http and server scope only,
  never inside a location block, so inheritance is the only way a location can
  have a non-default value — which is exactly why dropping it was silent."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server])
  (:import [java.io ByteArrayOutputStream]
           [java.net Socket SocketException SocketTimeoutException]))

(use-fixtures :each (server/make-fixture "timeout_location.conf"))

;; 2 s configured + the 5 ms sweep tick and CGI setup. 10 s is generous while
;; still far below the 60 s default a regression would fall back to.
(def ^:private deadline-ms 10000)

(defn- send-and-idle
  "Send payload, keep the write side OPEN, then read until the server responds
  or closes, or until timeout-ms elapses with no data. Half-closing would be
  detected as EOF on its own and would mask the timeout entirely."
  [payload timeout-ms]
  (with-open [sock (Socket. "127.0.0.1" 8080)]
    (.setSoTimeout sock timeout-ms)
    (let [out   (.getOutputStream sock)
          in    (.getInputStream sock)
          baos  (ByteArrayOutputStream.)
          buf   (byte-array 8192)
          start (System/currentTimeMillis)
          done  (fn [timed-out]
                  {:response   (String. (.toByteArray baos) "ISO-8859-1")
                   :timed-out  timed-out
                   :elapsed-ms (- (System/currentTimeMillis) start)})]
      (.write out (.getBytes payload "UTF-8"))
      (.flush out)
      (try
        (loop []
          (let [n (.read in buf)]
            (when (pos? n)
              (.write baos buf 0 n)
              (recur))))
        (done false)
        (catch SocketTimeoutException _ (done true))
        (catch SocketException _ (done false))))))

(deftest test-location-inherits-server-header-timeout
  (testing "a CGI POST with a stalled body honours the server's client_header_timeout, not the 60 s default"
    (let [{:keys [timed-out elapsed-ms]}
          (send-and-idle (str "POST /cgi-bin/hello.py HTTP/1.1\r\n"
                              "Host: 127.0.0.1\r\n"
                              "Content-Length: 100\r\n\r\n"
                              "only-a-few-bytes")
                         deadline-ms)]
      (is (not timed-out)
          (str "connection still open after " elapsed-ms " ms — config::header "
               "is not reaching the Location, so handle_setup() reset _timeout "
               "to the 60 s default (Gaps_and_Issues.md issue 8)")))))

(deftest test-static-request-still-served
  (testing "the same config serves an ordinary GET normally (control)"
    (is (= 200 (server/status-code
                 (server/raw-request "127.0.0.1" 8080
                   "GET /index.html HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"))))))
