(ns webserv-tests.timeout-test
  "Idle-client timeout regression tests (subject: 'a request to your server
  should never hang indefinitely').

  EpollLoop::run() sweeps every connection each tick and calls
  handle_timeout(), which answers 408 while the connection is still reading.
  That works — for the request-line/header phase, where _timeout still holds
  the value ClientConnection's ctor copied from the *server's* config::header.

  handle_setup() then ends with

      _timeout = _loc->get_header().timeout;

  and config::header is never inherited into a Location: Location::from_server()
  copies root/body/output/mime/errorpages/index/autoindex but not header, and
  Location::operator= drops it too (the parser also rejects
  client_header_timeout inside a location block, so there is no way to set it
  there either). Every connection therefore silently reverts to the built-in
  60 s default the moment its headers are parsed — a client that stalls
  mid-body holds a connection, an fd and a half-written upload for a minute,
  whatever the config says.

  Both tests run against timeout_body.conf, which sets client_header_timeout
  and client_body_timeout to 2 s. The header test is the control (passes
  today); the body test is the defect. Neither may half-close the write side:
  an EOF is detected on its own and would answer regardless of any timeout —
  the point is a client that stays connected and silent."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server])
  (:import [java.io ByteArrayOutputStream File]
           [java.net Socket SocketException SocketTimeoutException]))

(use-fixtures :each (server/make-fixture "timeout_body.conf"))

;; 2 s configured timeout + the 5 ms sweep tick; 6 s is generous but still far
;; below the 60 s default the connection wrongly falls back to.
(def ^:private deadline-ms 6000)

(defn- send-and-idle
  "Send payload, keep the write side OPEN, then read until the server responds
  or closes, or until timeout-ms elapses with no data.

  Returns {:response string :timed-out bool :elapsed-ms long}. :timed-out true
  means the server left an idle client hanging past the deadline."
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
        ;; A reset ends the read like an EOF: the peer is gone, not hanging.
        (catch SocketException _ (done false))))))

(deftest test-stalled-headers-time-out
  (testing "a client that stops mid-header-block is timed out within client_header_timeout (control)"
    (let [{:keys [response timed-out elapsed-ms]}
          (send-and-idle "GET /index.html HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                         deadline-ms)]
      (is (not timed-out)
          (str "an idle client must not be held past the configured 2 s "
               "client_header_timeout (waited " elapsed-ms " ms)"))
      (is (= 408 (server/status-code response))
          "a read-side timeout should produce 408 Request Timeout"))))

(deftest test-stalled-body-times-out
  (testing "KNOWN-FAILING: a client that stops mid-body is timed out too, not held for the 60 s default"
    (let [probe (File. "../www/timeout_body_probe.txt")]
      (when (.exists probe) (.delete probe))
      (try
        (let [{:keys [response timed-out elapsed-ms]}
              (send-and-idle (str "POST /timeout_body_probe.txt HTTP/1.1\r\n"
                                  "Host: 127.0.0.1\r\n"
                                  "Content-Length: 5000\r\n\r\n"
                                  "abc")
                             deadline-ms)]
          (is (not timed-out)
              (str "the connection was still open after " elapsed-ms
                   " ms; handle_setup() resets _timeout to the location's "
                   "60 s default because config::header is not inherited"))
          (is (or (empty? response) (= 408 (server/status-code response)))
              "closing is acceptable, 408 Request Timeout is better; a 2xx is not"))
        (finally
          (when (.exists probe) (.delete probe)))))))
