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
