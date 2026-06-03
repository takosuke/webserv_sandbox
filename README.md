## Caching Responses and Errorpages

Since creating a response includes allocating memory for buffers and other
things we could encounter an error where we would not even be able to send a
`500` error back to the user. To prevent this we decided to cache all errorpages
for the different servers.

Since different servers migth call the same errorpages they pages should be
with their name and be able to be referenced in different pages.

Inside each location an `config::errors` sits that contains a map of errorcodes
for the location and their respective errorpages. `config::errors` is mostly a
way to register new erropages in a seperate global singleton that caches the
pages based on their path. `config::errors` checks for the existence of a page
in the singleton and adds the file if not present, if the page already exists
just marks the page for the errorcode.

Since multiple errorcodes can refer to a singe errorpage the `Response` itself
should be able to grab the proper status-line from it's cache of initilaized
status-lines based on the code we have at hand when requesting the errorpage.

## Request -> Response Flow

When a new Connection is opened and a Request is processed first the status line
needs to used to determine the server and location the request is for.

Based on these informations we can check for maximum header size and so forth.
If at any point during this validation of status-line and headers an error
occurs (f.e. because the file doesn't exist of the headers are too large) the
errorpages of the location can be used to get the appropriate
`struct config::errorpageinfo` in which a response code and a filepath for the
given errorcode is saved. The `internal` bool inside `config::errorpageinfo` is
used to determine whether the filepath should be requested through the `Server`
to get the new file that should be served or whether the client should be sent a
302 response with the filepath as a `Location` header field.

After the headers are parsed completely the next step
