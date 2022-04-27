# Cerver [C-Server]

A simple HTTP server implemented in C. Just for learning purpose, not recommended for any environment.

```
> make
> ./cerver <port>
```

Visit http://127.0.0.1/public/

- Serve only static files
- Only support GET and HEAD, and limited mime types
- Concurrent at max 8 connections using prethreading