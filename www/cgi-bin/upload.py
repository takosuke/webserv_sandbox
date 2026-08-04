#!/usr/bin/env python3
import os
import sys

# Default upload directory: <wwwroot>/uploads, derived from SCRIPT_FILENAME
# (root + path) so it works regardless of the server's cwd.
# Once the server grows an `upload_path` directive, it can pass it down as
# the UPLOAD_DIR env var (via cgi_param) and this script needs no changes.
def default_upload_dir():
	script_filename = os.environ.get("SCRIPT_FILENAME", "")
	cgi_bin_dir = os.path.dirname(script_filename)
	www_root = os.path.dirname(cgi_bin_dir)
	return os.path.join(www_root, "uploads")

UPLOAD_DIR = os.environ.get("UPLOAD_DIR", default_upload_dir())

FORM_HTML = """<!DOCTYPE html>
<html>
<head><title>Upload</title></head>
<body>
<h1>Upload a file</h1>
<form method="POST" enctype="multipart/form-data" action="/cgi-bin/upload.py">
	<input type="file" name="file">
	<input type="submit" value="Upload">
</form>
</body>
</html>
"""

def respond(status, body, content_type="text/html"):
	body_bytes = body.encode("utf-8") if isinstance(body, str) else body
	sys.stdout.buffer.write(("Status: " + status + "\r\n").encode("ascii"))
	sys.stdout.buffer.write(("Content-Type: " + content_type + "\r\n").encode("ascii"))
	sys.stdout.buffer.write(("Content-Length: " + str(len(body_bytes)) + "\r\n").encode("ascii"))
	sys.stdout.buffer.write(b"\r\n")
	sys.stdout.buffer.write(body_bytes)
	sys.stdout.buffer.flush()
	sys.exit(0)

def read_body():
	try:
		content_length = int(os.environ.get("CONTENT_LENGTH", "0"))
	except ValueError:
		content_length = 0
	if content_length <= 0:
		return b""
	return sys.stdin.buffer.read(content_length)

# Pulls the boundary token out of a Content-Type header such as:
#   multipart/form-data; boundary=----WebKitFormBoundaryXYZ
# NB: this server currently lowercases header *values* along with keys
# before they reach the CGI env, which corrupts any boundary containing
# uppercase characters. Browser-generated boundaries are usually mixed
# case, so uploads may fail to find the boundary until that's fixed
# server-side. Not something to work around here -- the fix belongs in
# ClientConnection::handle_req_headers().
def extract_boundary(content_type):
	for part in content_type.split(";"):
		part = part.strip()
		if part.startswith("boundary="):
			boundary = part[len("boundary="):]
			return boundary.strip('"')
	return None

def parse_multipart(body, boundary):
	delimiter = b"--" + boundary.encode("utf-8")
	sections = body.split(delimiter)
	for section in sections:
		section = section.strip(b"\r\n")
		if not section or section == b"--":
			continue
		if b"\r\n\r\n" not in section:
			continue
		headers_blob, content = section.split(b"\r\n\r\n", 1)
		headers_text = headers_blob.decode("utf-8", errors="replace")
		disposition = None
		for line in headers_text.split("\r\n"):
			if line.lower().startswith("content-disposition:"):
				disposition = line
				break
		if disposition is None or "filename=" not in disposition:
			continue
		filename = None
		for token in disposition.split(";"):
			token = token.strip()
			if token.startswith("filename="):
				filename = token[len("filename="):].strip('"')
				break
		if not filename:
			continue
		# content ends right before the trailing CRLF that precedes the
		# next boundary delimiter
		if content.endswith(b"\r\n"):
			content = content[:-2]
		return filename, content
	return None, None

def sanitize_filename(filename):
	# Strip any directory components the client tried to sneak in
	# (path traversal, absolute paths) -- only the basename is trusted.
	filename = filename.replace("\\", "/")
	filename = os.path.basename(filename)
	if not filename or filename in (".", ".."):
		return None
	return filename

def handle_post():
	content_type = os.environ.get("CONTENT_TYPE", "")
	if not content_type.lower().startswith("multipart/form-data"):
		respond("400 Bad Request", "<h1>Expected multipart/form-data</h1>")

	boundary = extract_boundary(content_type)
	if not boundary:
		respond("400 Bad Request", "<h1>Missing multipart boundary</h1>")

	body = read_body()
	filename, content = parse_multipart(body, boundary)
	if filename is None:
		respond("400 Bad Request", "<h1>No file field found in upload</h1>")

	filename = sanitize_filename(filename)
	if filename is None:
		respond("400 Bad Request", "<h1>Invalid filename</h1>")

	os.makedirs(UPLOAD_DIR, exist_ok=True)
	dest_path = os.path.join(UPLOAD_DIR, filename)
	try:
		with open(dest_path, "wb") as f:
			f.write(content)
	except OSError:
		respond("500 Internal Server Error", "<h1>Could not save file</h1>")

	respond("201 Created", "<h1>Uploaded '" + filename + "' (" + str(len(content)) + " bytes)</h1>")

def main():
	method = os.environ.get("REQUEST_METHOD", "GET")
	if method == "POST":
		handle_post()
	else:
		respond("200 OK", FORM_HTML)

if __name__ == "__main__":
	main()
