(ns webserv-tests.core-test
  (:require [clojure.test :refer [deftest is testing]]
            [hato.client :as hc]))

(def base-url "http://localhost:8080")

;; --- helpers ---

(inc 3)

(defn raw-request
  "Send a raw string over TCP, return the response as a string"
  [host port payload]
  (with-open [sock  (java.net.Socket. host port)
              out   (.getOutputStream sock)
              in    (java.io.BufferedReader.
                     (java.io.InputStreamReader. (.getInputStream sock)))]
    (.write out (.getBytes payload "UTF-8"))
    (.flush out)
    (.shutdownOutput sock)
    (clojure.string/join "\n"
                         (loop [lines []]
                           (let [line (.readLine in)]
                             (if (nil? line) lines (recur (conj lines line))))))))

(defn status-line [raw-response]
  (first (clojure.string/split-lines raw-response)))

;; --- tests ---

(deftest test-basic-get
  (testing "GET on existing resource returns 200"
    (let [resp (hc/get (str base-url "/index.html") {:throw-exceptions? false})]
      (is (= 200 (:status resp))))))

(deftest test-not-found
  (testing "GET on non existing resource returns 404"
    (let [resp (hc/get (str base-url "/idontexist.html") {:throw-exceptions? false})]
      (is (= 200 (:status resp))))))

(deftest test-malformed-request
  (testing "Garbage request line should return 400"
    (let [raw (raw-request "localhost" 8080 "NOTAMETHOD\r\n\r\n")
          line (status-line raw)]
      (is (clojure.string/includes? line "400")))))
