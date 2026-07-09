(ns webserv-tests.method-test
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

;; Only safe tests here: requests whose methods the server handles without
;; crashing (GET, POST, DELETE).  Tests for unknown/unimplemented methods
;; that return 501 are in error_response_test.clj because they crash the
;; server before the Location pointer is resolved.
(use-fixtures :once (server/make-fixture "base.conf"))

(deftest test-get-existing-file
  (testing "GET returns 200 for an existing file"
    (is (= 200 (:status (server/http-get "/index.html"))))))

;; POST and DELETE are recognised methods (no 501 from the request-line parser)
;; but the server has no handler body for them.  Verify the server doesn't crash.

(deftest test-post-does-not-crash-server
  (testing "Server stays alive after a POST request"
    (server/raw-request "127.0.0.1" 8080
      "POST / HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 5\r\n\r\nhello")
    (is (= 200 (:status (server/http-get "/index.html"))))))

;; NB: DELETE actually deletes now — this test must target a scratch file it
;; creates itself, never a shared asset like /index.html (that once wiped
;; www/index.html and cascaded 302s through the whole suite).
(deftest test-delete-does-not-crash-server
  (testing "Server stays alive after a DELETE request"
    (let [f (java.io.File. "../www/delete_probe.txt")]
      (spit f "probe")
      (try
        (server/raw-request "127.0.0.1" 8080
          "DELETE /delete_probe.txt HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
        (is (= 200 (:status (server/http-get "/index.html"))))
        (finally
          (when (.exists f) (.delete f)))))))

(deftest test-multiple-sequential-gets
  (testing "Server handles multiple sequential GET requests without crashing"
    (is (= [200 200 200]
           (mapv #(:status (server/http-get %))
                 ["/index.html" "/static/index.html" "/static/css/style.css"])))))
