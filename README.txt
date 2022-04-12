I only print two additional messages for the option -v.
One for server mode, when accepting a client, 
like: Connection received on localhost 55176

Another one for client mode, when connecting server,
line: Connection to localhost (127.0.0.1) 1234 port [tcp/*] succeeded!

I have test the nc command with -v,
it just print these two messages too.
I'm not sure it's enought to just print these verbose info.

The verbose info of a client:
"Connection to localhost (127.0.0.1) 1234 port [tcp/*] succeeded!"
is printed when a socket to server is created,
but it's not sure that the client is accepted by server,
because the server has a limitation of connection number,
the client may not be able to communicate with server until some client quit 
even the verbose info printed.
I wonder if there are methods to determine whether the socket is accepted 
by server without writing data to the socket.
