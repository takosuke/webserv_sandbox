(ns webserv-tests.static-files-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "base.conf"))

;; ---- existing files ----

(deftest test-root-index
  (testing "GET /index.html returns 200"
    (is (= 200 (:status (server/http-get "/index.html"))))))

(deftest test-static-subdirectory
  (testing "GET /static/index.html returns 200"
    (is (= 200 (:status (server/http-get "/static/index.html"))))))

(deftest test-css-file
  (testing "GET /static/css/style.css returns 200"
    (is (= 200 (:status (server/http-get "/static/css/style.css"))))))

;; ---- file not found ----

(deftest test-missing-file-is-error
  (testing "GET on a non-existent path returns an error status"
    (is (>= (:status (server/http-get "/does-not-exist.html")) 400))))

;; ---- body content ----

(deftest test-index-body-contains-doctype
  (testing "Body of /index.html starts with a DOCTYPE or html tag"
    (let [body (:body (server/http-get "/index.html"))]
      (is (or (clojure.string/includes? body "<!DOCTYPE")
              (clojure.string/includes? body "<html"))))))

(deftest test-index-body-contains-hello-world
  (testing "Body of /index.html contains the expected page text"
    (is (clojure.string/includes? (:body (server/http-get "/index.html")) "Hello"))))

;; ---- path normalisation ----

(deftest test-dot-segments-normalised
  (testing "GET /static/../index.html resolves to /index.html and returns 200"
    (is (= 200 (:status (server/http-get "/static/../index.html"))))))
