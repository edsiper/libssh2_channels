# LibSSH2 Port Forwarding Test case

The program connects to the SSH server running at localhost:22 and after authentication it request to forward the localhost:9000 TCP port over the SSH tunnel.

For testing purposes make sure you edit fwd-client.c and modify the _username_ and place your certificates _id\_rsa.prv_ and _id\_rsa.pub_.

# How it works

After the client runs and establish a session, it waits for events on the socket connected to the server. When a client connects, it reads some data and write back some HTTP response, it can be tested with _curl_ as follows:

```Shell
$ curl -i http://localhost:9000
HTTP/1.0 200 OK
Connection: close
Content-Length: 10

abcd efgh
```

One connection works.

# The problem

If this code faced multiple requests (multiple channels) for some reason it fails to handle channels data properly. This can be tested with _Apache Benchmark_:

```Shell
$ ab -n 10 -c 2 http://localhost:9000/
This is ApacheBench, Version 2.3 <$Revision: 1528965 $>
Copyright 1996 Adam Twiss, Zeus Technology Ltd, http://www.zeustech.net/
Licensed to The Apache Software Foundation, http://www.apache.org/

Benchmarking localhost (be patient).....done


Server Software:
Server Hostname:        localhost
Server Port:            9000

Document Path:          /
Document Length:        0 bytes

Concurrency Level:      2
Time taken for tests:   0.046 seconds
Complete requests:      10
Failed requests:        9
   (Connect: 0, Receive: 0, Length: 5, Exceptions: 4)
   Total transferred:      340 bytes
   HTML transferred:       50 bytes
   Requests per second:    215.70 [#/sec] (mean)
   Time per request:       9.272 [ms] (mean)
   Time per request:       4.636 [ms] (mean, across all concurrent requests)
   Transfer rate:          7.16 [Kbytes/sec] received
...
```

Allmost all connections failed, why ?...
