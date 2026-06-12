(ns webserv-tests.response-format-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "base.conf"))

;; ---- status line ----

(deftest test-status-line-format
  (testing "200 response status line matches HTTP/x.x 200 <reason>"
    (let [raw (server/raw-request "127.0.0.1" 8080
                "GET /index.html HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")]
      (is (re-find #"^HTTP/\d\.\d 200 " raw)))))

;; ---- mandatory response headers ----

(deftest test-content-length-present
  (testing "200 response includes a Content-Length header"
    (let [resp (server/http-get "/index.html")]
      (is (contains? (:headers resp) "content-length")))))

(deftest test-date-header-present
  (testing "200 response includes a Date header"
    (let [resp (server/http-get "/index.html")]
      (is (contains? (:headers resp) "date")))))

;; ---- content-length accuracy ----

(deftest test-content-length-is-positive
  (testing "Content-Length for an HTML file is a positive integer"
    (let [resp (server/http-get "/index.html")
          cl   (some-> (get-in resp [:headers "content-length"]) Integer/parseInt)]
      (is (and cl (pos? cl))))))

;; ---- response body ----

(deftest test-body-non-empty-for-html
  (testing "Response body for /index.html is not empty"
    (let [resp (server/http-get "/index.html")]
      (is (seq (:body resp))))))

(deftest test-body-contains-html
  (testing "Body of /index.html contains an HTML tag"
    (let [resp (server/http-get "/index.html")]
      (is (clojure.string/includes? (:body resp) "<html")))))

;; ---- allow header ----

(deftest test-allow-header-present
  (testing "GET response includes an Allow header listing permitted methods"
    (let [resp (server/http-get "/index.html")]
      (is (contains? (:headers resp) "allow")))))
