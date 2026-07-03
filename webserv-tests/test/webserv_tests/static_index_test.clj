(ns webserv-tests.static-index-test
  "Regression test for the `index` directive path bug (see Architecture.md
  \"Additional issues found by source review\"). handle_setup sets
  _req.path = <index-file> instead of appending the index to the requested
  directory, so GET /idxdir/ serves the ROOT index.html rather than
  www/idxdir/index.html.

  The subdir index carries a unique marker; a correct server returns that
  marker, the buggy server returns the root index. KNOWN-FAILING until fixed.
  These requests don't crash the server, so a single :once fixture is fine."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [webserv-tests.server :as server]))

(use-fixtures :once (server/make-fixture "subdir_index.conf"))

(deftest test-subdirectory-index-is-served
  (testing "KNOWN-FAILING: GET /idxdir/ serves www/idxdir/index.html, not the root index"
    (let [resp (server/http-get "/idxdir/")]
      (is (= 200 (:status resp)))
      (is (clojure.string/includes? (:body resp) "SUBDIR-INDEX-MARKER-9F3A")
          "the directory's own index.html should be served, not the root one"))))

(deftest test-root-index-still-served
  (testing "GET / still serves the root index (control: index directive works at all)"
    ;; Not KNOWN-FAILING: at the root the index-file path and the directory
    ;; happen to coincide, so this should pass today and guards against a fix
    ;; that breaks the root case.
    (let [resp (server/http-get "/")]
      (is (= 200 (:status resp))))))
