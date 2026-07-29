(ns webserv-tests.autoindex-test
  "Directory-listing regression tests.

  setup_autoindex() renders the listing in-process (autoindex::as_html over
  opendir/readdir — no fork, which is what the subject wants) but stages it
  through a temp file: mkstemp(\"/tmp/autoindex_XXXXXX\"), write, reopen for
  reading, serve. Unlike finalize_cgi(), which std::remove()s its /tmp/cgi_*
  file, nothing ever unlinks the autoindex one — so a server that lists
  directories drips one file into /tmp per request, for the lifetime of the
  process.

  Staging through a temp file is fine (mkstemp is allowed); leaking it is not.
  The fix is the unlink-while-open idiom already used for CGI: remove the name
  right after reopening the stream for reading, and the inode disappears when
  the stream closes.

  The listing is also built with a hand-rolled header block that never sets
  Content-Type, so browsers get generated HTML with no type at all."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [clojure.set :as set]
            [clojure.string :as str]
            [webserv-tests.server :as server])
  (:import [java.io File]))

(defn- tmp-autoindex-files
  "Every /tmp/autoindex_* left behind, as a set of names."
  []
  (set (map #(.getName %)
            (filter #(str/starts-with? (.getName %) "autoindex_")
                    (or (seq (.listFiles (File. "/tmp"))) [])))))

(defn- sweep-leaked-tmp-files
  "Every test in this namespace makes the server leak, not just the one that
  asserts about it. Snapshot /tmp once and remove anything this run added, so
  the suite leaves the machine as it found it."
  [run-tests]
  (let [before (tmp-autoindex-files)]
    (try (run-tests)
         (finally
           (doseq [name (set/difference (tmp-autoindex-files) before)]
             (.delete (File. (str "/tmp/" name))))))))

(use-fixtures :once (server/make-fixture "autoindex.conf") sweep-leaked-tmp-files)

(deftest test-autoindex-lists-directory
  (testing "GET on a directory without an index serves a generated listing (control)"
    (let [resp (server/http-get "/autoindex/")]
      (is (= 200 (:status resp)))
      (is (str/includes? (:body resp) "unhidden-file")
          "the listing should name the files in the directory"))))

(deftest test-autoindex-does-not-leak-temp-files
  (testing "KNOWN-FAILING: serving directory listings leaves no /tmp/autoindex_* files behind"
    (let [before (tmp-autoindex-files)]
      (dotimes [_ 5] (server/http-get "/autoindex/"))
      (let [leaked (set/difference (tmp-autoindex-files) before)]
        (is (empty? leaked)
            (str "5 directory listings leaked " (count leaked)
                 " temp file(s) into /tmp; setup_autoindex never unlinks them"))))))

(deftest test-autoindex-has-html-content-type
  (testing "KNOWN-FAILING: a generated directory listing is served as text/html"
    (let [resp (server/http-get "/autoindex/")]
      (is (str/includes? (str (get-in resp [:headers "content-type"])) "text/html")
          "setup_autoindex builds its own header block and omits Content-Type"))))
