# TFTP

TFTP is a small and very safe mini ftp server. You can read more about it [here].

This is the repo for my implementation of a TFTP server and it's respective client for the Operating Systems class that I'm attending.

## Changelog
- Server
    - Launches a child process for each file request or a thread for each file request, depending on your OperationMode flag (1 for threads, 0 for forks)
- Client
    - Remote ls (list) all files in given dir, using forks
    - Remote mget (multiple get) - retrieves every file in the given dir, using forks
    - Elapsed time in microseconds

## Initial functionalities
- Server
    - Bind to a port in the host computer
    - Serve one file at a time
- Client
    - GET [fileName] - Retrieve the file from the server
    - PUT [fileName] - Put the file in the server



[//]:# (Links come here // http://stackoverflow.com/questions/4823468/store-comments-in-markdown-syntax)

[here]: https://en.wikipedia.org/wiki/Trivial_File_Transfer_Protocol