# Serveur-Web-Multi-threaded

The goal of the project was to transform a simple single-threaded web server into a high-performance multithreaded web server. This change aims to address performance issues inherent to the single-threaded model, where only one HTTP request can be handled at a time.

### Main Objectives

- Add support for multiple threads to enable concurrent processing of HTTP requests.
- Introduce a buffer queue to synchronize requests between the master thread and worker threads.
- Ensure proper synchronization between threads using condition variables, avoiding any spin-waiting mechanisms.
- Protect the server against insecure requests (e.g., directory traversal using `..`).
