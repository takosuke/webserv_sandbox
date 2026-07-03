(ns webserv-tests.server
  (:import [java.net Socket ConnectException]
           [java.io File]))

(def ^:private binary  "../webserv")
(def ^:private conf-dir "../conf/generated/")

;; --- process management ---

(defn start!
  "Launch ../webserv with conf/generated/<conf-name>. Returns the Process."
  [conf-name]
  (-> (ProcessBuilder. [binary (str conf-dir conf-name)])
      (.redirectOutput (File. "/dev/null"))
      (.redirectError  (File. "/dev/null"))
      .start))

(defn stop!
  "Send SIGKILL to proc and block until it exits."
  [^Process proc]
  (.destroyForcibly proc)
  (.waitFor proc))

(defn- port-open? [port]
  (try (doto (Socket. "127.0.0.1" port) .close) true
       (catch ConnectException _ false)))

(defn- wait-for-port-open [port timeout-ms]
  "Block until 127.0.0.1:port accepts a TCP connection."
  (let [deadline (+ (System/currentTimeMillis) timeout-ms)]
    (loop []
      (if (> (System/currentTimeMillis) deadline)
        (throw (ex-info "Timed out waiting for server to start" {:port port}))
        (if (port-open? port)
          nil
          (do (Thread/sleep 50) (recur)))))))

(defn- wait-for-port-free [port timeout-ms]
  "Block until nothing is listening on 127.0.0.1:port (old server fully gone)."
  (let [deadline (+ (System/currentTimeMillis) timeout-ms)]
    (loop []
      (when (and (port-open? port)
                 (< (System/currentTimeMillis) deadline))
        (Thread/sleep 50)
        (recur)))))

(def ^:dynamic *conf-name*
  "Name of the conf file the current fixture launched webserv with. Bound by
  make-fixture for the duration of a test run; read by the failure reporter
  (see webserv-tests.report) and available to tests if needed."
  nil)

(defn make-fixture
  "clojure.test fixture: starts webserv with conf-name, waits up to 5 s for
  port 8080 to accept connections, runs the tests with *conf-name* bound, then
  stops the server. The binding lets the failure reporter print the conf file
  under each failed test (see webserv-tests.report)."
  [conf-name]
  (fn [run-tests]
    (let [proc (start! conf-name)]
      (binding [*conf-name* conf-name]
        (try
          (wait-for-port-open 8080 5000)
          (run-tests)
          (finally
            (stop! proc)
            (wait-for-port-free 8080 3000)))))))

;; --- shared HTTP helpers ---

(defn raw-request
  "Write payload over a raw TCP socket to host:port, return the full response as a string."
  [host port payload]
  (with-open [sock (Socket. host port)
              out  (.getOutputStream sock)
              in   (java.io.BufferedReader.
                     (java.io.InputStreamReader. (.getInputStream sock)))]
    (.write out (.getBytes payload "UTF-8"))
    (.flush out)
    (.shutdownOutput sock)
    (clojure.string/join "\n"
      (loop [lines []]
        (let [line (.readLine in)]
          (if (nil? line) lines (recur (conj lines line))))))))

(defn raw-request-timeout
  "Like raw-request, but installs a read timeout so a hanging/spinning server
  cannot block the test suite forever. Sends payload, half-closes the write
  side, then reads until EOF or timeout-ms elapses with no data.

  Returns {:response <string, possibly partial or empty> :timed-out <bool>}.
  A well-behaved server answers (or closes) before the deadline, so :timed-out
  is false. A server that hangs the request (e.g. an infinite decode loop) or
  never detects a half-closed peer leaves :timed-out true."
  [host port payload timeout-ms]
  (with-open [sock (Socket. host port)]
    (.setSoTimeout sock timeout-ms)
    (let [out (.getOutputStream sock)
          in  (.getInputStream sock)]
      (.write out (.getBytes payload "UTF-8"))
      (.flush out)
      (.shutdownOutput sock)
      (let [baos (java.io.ByteArrayOutputStream.)
            buf  (byte-array 8192)]
        (try
          (loop []
            (let [n (.read in buf)]
              (when (pos? n)
                (.write baos buf 0 n)
                (recur))))
          {:response (String. (.toByteArray baos) "ISO-8859-1") :timed-out false}
          (catch java.net.SocketTimeoutException _
            {:response (String. (.toByteArray baos) "ISO-8859-1") :timed-out true})
          ;; A RST/abrupt close ("Connection reset") ends the read like an EOF
          ;; would: the peer is gone, so it is not a hang. Return what we got
          ;; (often empty) so callers assert on the status line, not on an
          ;; uncaught exception.
          (catch java.net.SocketException _
            {:response (String. (.toByteArray baos) "ISO-8859-1") :timed-out false}))))))

(defn responsive?
  "Send a fresh, well-formed GET /index.html on a new connection with a read
  timeout and return true iff it comes back 200 within timeout-ms. Used as a
  liveness probe after a pathological request."
  [timeout-ms]
  (let [{:keys [response timed-out]}
        (raw-request-timeout "127.0.0.1" 8080
          "GET /index.html HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n" timeout-ms)]
    (and (not timed-out)
         (= 200 (when-let [[_ c] (re-find #"HTTP/\S+ (\d{3})" response)]
                  (Integer/parseInt c))))))

(defn status-code
  "Parse the HTTP status code integer out of a raw response string."
  [raw-response]
  (when-let [line (first (clojure.string/split-lines raw-response))]
    (when-let [[_ code] (re-find #"HTTP/\S+ (\d{3})" line)]
      (Integer/parseInt code))))

(defn parse-response
  "Parse a raw HTTP response string into {:status int :headers map :body string}.
  Headers map has lower-cased keys. Returns nil if raw is empty."
  [raw]
  (when (seq raw)
    (let [[head-part body-part] (clojure.string/split raw #"\n\n" 2)
          head-lines (clojure.string/split-lines (or head-part ""))
          [_ code _] (re-find #"HTTP/\S+ (\d{3}) ?(.*)" (or (first head-lines) ""))
          headers (into {}
                   (keep (fn [l]
                           (when-let [[_ k v] (re-find #"^([^:]+):\s*(.*)" l)]
                             [(clojure.string/lower-case (clojure.string/trim k)) (clojure.string/trim v)]))
                         (rest head-lines)))]
      {:status  (when code (Integer/parseInt code))
       :headers headers
       :body    (or body-part "")})))

(defn http-get
  "Send a minimal HTTP/1.1 GET and return a parsed response map."
  [path]
  (parse-response
    (raw-request "127.0.0.1" 8080
      (str "GET " path " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"))))

(defn http-request
  "Send a raw HTTP request string and return a parsed response map."
  [req]
  (parse-response (raw-request "127.0.0.1" 8080 req)))

;; --- binary-safe helpers ---
;;
;; raw-request reads the response through a UTF-8 Reader, which mangles binary
;; payloads. These helpers read the response as raw bytes so tests can check
;; that a served file arrives intact (e.g. a PNG with embedded NUL bytes).

(defn raw-request-bytes
  "Like raw-request, but returns the entire response as a byte array."
  [host port payload]
  (with-open [sock (Socket. host port)
              out  (.getOutputStream sock)
              in   (.getInputStream sock)
              baos (java.io.ByteArrayOutputStream.)]
    (.write out (.getBytes payload "UTF-8"))
    (.flush out)
    (.shutdownOutput sock)
    (let [buf (byte-array 8192)]
      (loop []
        (let [n (.read in buf)]
          (when (pos? n)
            (.write baos buf 0 n)
            (recur)))))
    (.toByteArray baos)))

(defn split-head-body-bytes
  "Split a raw response byte array at the first CRLFCRLF. Returns
  {:head <String, ISO-8859-1> :body <byte[]>}. If no separator is found the
  whole response is treated as the head and :body is empty."
  [^bytes resp]
  (let [len (alength resp)
        idx (loop [i 0]
              (cond
                (> (+ i 4) len) -1
                (and (== (aget resp i) 13)      ; \r
                     (== (aget resp (+ i 1)) 10) ; \n
                     (== (aget resp (+ i 2)) 13) ; \r
                     (== (aget resp (+ i 3)) 10)) ; \n
                i
                :else (recur (inc i))))]
    (if (neg? idx)
      {:head (String. resp "ISO-8859-1") :body (byte-array 0)}
      {:head (String. resp 0 idx "ISO-8859-1")
       :body (java.util.Arrays/copyOfRange resp (+ idx 4) len)})))

(defn http-get-bytes
  "Send a minimal HTTP/1.1 GET and return {:status :content-length :body-bytes}.
  :content-length is the integer value of the response header, or nil if absent.
  :body-bytes is the raw response body as a byte array (binary-safe)."
  [path]
  (let [{:keys [head body]} (split-head-body-bytes
                              (raw-request-bytes "127.0.0.1" 8080
                                (str "GET " path " HTTP/1.1\r\n"
                                     "Host: 127.0.0.1\r\nConnection: close\r\n\r\n")))
        status (when-let [[_ c] (re-find #"HTTP/\S+ (\d{3})" head)]
                 (Integer/parseInt c))
        clen   (when-let [[_ c] (re-find #"(?i)content-length:\s*(\d+)" head)]
                 (Integer/parseInt c))]
    {:status status :content-length clen :body-bytes body}))
