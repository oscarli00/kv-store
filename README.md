# kv-store

An in-memory key-value store inspired by Redis.
 
## Features

- Uses a non-blocking event-driven server which is able to handle mulitple clients concurrently.

- Uses the Linux system call, ```epoll```, to more efficiently monitor mulitple file descriptors for activity compared to ```poll``` and ```select```.

- Implements ```get```, ```set``` and ```del``` commands.

## How To Use

Note: the project requires Linux.

1. Generate build (```cmake build```)
1. Build using cmake (```cmake --build /build```)
2. Run the server
3. Run the client
4. Enter commands into the client terminal such as:

    - ```get [KEY]```

    - ```set [KEY] [VALUE]```

    - ```del [KEY]```

## Future Work

- Add support for different data types such as arrays.

- Implement timeouts for idle client connections.

- Implement TTLs for cached data.