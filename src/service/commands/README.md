# rdss Commands

Given that the commands are largely compatible with Redis, this page heavily relies on Redis' documentation.

## Strings

<details>
<summary>SET</summary>

> Set key to hold the string value. If key already holds a value, it is overwritten, regardless of its type. Any previous time to live associated with the key is discarded on successful SET operation.

### Syntax

```
SET key value [NX | XX] [GET] [EX seconds | PX milliseconds | EXAT unix-time-seconds | PXAT unix-time-milliseconds | KEEPTTL]
```

### Options

- EX *seconds* -- Set the specified expire time, in seconds (a positive integer).
- PX *milliseconds* -- Set the specified expire time, in milliseconds (a positive integer).
- EXAT *timestamp-seconds* -- Set the specified Unix time at which the key will expire, in seconds (a positive integer).
- PXAT *timestamp-milliseconds* -- Set the specified Unix time at which the key will expire, in milliseconds (a positive integer).
- NX -- Only set the key if it does not already exist.
- XX -- Only set the key if it already exists.
- KEEPTTL -- Retain the time to live associated with the key.
- GET -- Return the old string stored at key, or nil if key did not exist. An error is returned and SET aborted if the value stored at key is not a string.

### Reply

- Null reply: GET not given: Operation was aborted (conflict with one of the XX/NX options).
- Simple string reply: OK. GET not given: The key was set.
- Null reply: GET given: The key didn't exist before the SET.
- Bulk string reply: GET given: The previous value of the key.

</details>

<details>
<summary>GET</summary>

> Get the value of key. If the key does not exist the special value nil is returned. An error is returned if the value stored at key is not a string, because GET only handles string values.

### Syntax

```
GET key
```

### Reply

- Bulk string reply: the value of the key.
- Null reply: key does not exist.

</details>

<details>
<summary>SETNX</summary>

> Set key to hold string value if key does not exist. In that case, it is equal to SET. When key already holds a value, no operation is performed. SETNX is short for "SET if Not eXists".

### Syntax

```
SETNX key value
```

### Reply

- Integer reply: 0 if the key was not set.
- Integer reply: 1 if the key was set.

</details>

<details>
<summary>SETEX</summary>

> Set key to hold the string value and set key to timeout after a given number of seconds.

### Syntax

```
SETEX key seconds value
```

### Reply

- Simple string reply: OK.

</details>

<details>
<summary>PSETEX</summary>

> PSETEX works exactly like SETEX with the sole difference that the expire time is specified in milliseconds instead of seconds.

### Syntax

```
PSETEX key milliseconds value
```

### Reply

- Simple string reply: OK.

</details>

<details>
<summary>SETRANGE</summary>

> Overwrites part of the string stored at key, starting at the specified offset, for the entire length of value. If the offset is larger than the current length of the string at key, the string is padded with zero-bytes to make offset fit. Non-existing keys are considered as empty strings, so this command will make sure it holds a string large enough to be able to set value at offset.

### Syntax

```
SETRANGE key offset value
```

### Reply

- Integer reply: the length of the string after it was modified by the command.

</details>

<details>
<summary>MSET</summary>

> Sets the given keys to their respective values. MSET replaces existing values with new values, just as regular SET. See MSETNX if you don't want to overwrite existing values.  
MSET is atomic, so all given keys are set at once. It is not possible for clients to see that some of the keys were updated while others are unchanged.

### Syntax

```
MSET key value [key value ...]
```

### Reply

- Simple string reply: always OK because MSET can't fail.

</details>

<details>
<summary>MSETNX</summary>

> Sets the given keys to their respective values. MSETNX will not perform any operation at all even if just a single key already exists.  
Because of this semantic MSETNX can be used in order to set different keys representing different fields of a unique logic object in a way that ensures that either all the fields or none at all are set.  
MSETNX is atomic, so all given keys are set at once. It is not possible for clients to see that some of the keys were updated while others are unchanged.

### Syntax

```
MSETNX key value [key value ...]
```

### Reply

- Integer reply: 0 if no key was set (at least one key already existed).
- Integer reply: 1 if all the keys were set.

</details>

<details>
<summary>APPEND</summary>

> If key already exists and is a string, this command appends the value at the end of the string. If key does not exist it is created and set as an empty string, so APPEND will be similar to SET in this special case.

### Syntax

```
APPEND key value
```

### Reply

- Integer reply: the length of the string after the append operation.

</details>

<details>
<summary>GETEX</summary>

> Get the value of key and optionally set its expiration. GETEX is similar to GET, but is a write command with additional options.

### Syntax

```
GETEX key [EX seconds | PX milliseconds | EXAT unix-time-seconds | PXAT unix-time-milliseconds | PERSIST]
```

### Options

- EX *seconds* -- Set the specified expire time, in seconds.
- PX *milliseconds* -- Set the specified expire time, in milliseconds.
- EXAT *timestamp-seconds* -- Set the specified Unix time at which the key will expire, in seconds.
- PXAT *timestamp-milliseconds* -- Set the specified Unix time at which the key will expire, in milliseconds.
- PERSIST -- Remove the time to live associated with the key.

### Reply

- Bulk string reply: the value of key
- Null reply: if key does not exist.

</details>

<details>
<summary>GETDEL</summary>

> Get the value of key and delete the key. This command is similar to GET, except for the fact that it also deletes the key on success (if and only if the key's value type is a string).

### Syntax

```
GETDEL key
```

### Reply

- Bulk string reply: the value of the key.
- Null reply: if the key does not exist or if the key's value type is not a string.

</details>

<details>
<summary>GETSET</summary>

> Atomically sets key to value and returns the old value stored at key. Returns an error when key exists but does not hold a string value. Any previous time to live associated with the key is discarded on successful SET operation.

### Syntax

```
GETSET key value
```

### Reply

- Bulk string reply: the old value stored at the key.
- Null reply: if the key does not exist.

</details>

<details>
<summary>GETRANGE</summary>

> Returns the substring of the string value stored at key, determined by the offsets start and end (both are inclusive). Negative offsets can be used in order to provide an offset starting from the end of the string. So -1 means the last character, -2 the penultimate and so forth.  
The function handles out of range requests by limiting the resulting range to the actual length of the string.

### Syntax

```
GETRANGE key start end
```

### Reply

- Bulk string reply: The substring of the string value stored at key, determined by the offsets start and end (both are inclusive).

</details>

<details>
<summary>MGET</summary>

> Returns the values of all specified keys. For every key that does not hold a string value or does not exist, the special value nil is returned. Because of this, the operation never fails.

### Syntax

```
MGET key [key ...]
```

### Reply

- Array reply: a list of values at the specified keys.

</details>

<details>
<summary>STRLEN</summary>

> Returns the length of the string value stored at key. An error is returned when key holds a non-string value.

### Syntax

```
STRLEN key
```

### Reply

- Integer reply: the length of the string stored at key, or 0 when the key does not exist.

</details>

</details>

<details>
<summary>SUBSTR</summary>

> Returns the substring of the string value stored at key, determined by the offsets start and end (both are inclusive). Negative offsets can be used in order to provide an offset starting from the end of the string. So -1 means the last character, -2 the penultimate and so forth.  
The function handles out of range requests by limiting the resulting range to the actual length of the string.

### Syntax

```
SUBSTR key start end
```

### Reply

- Bulk string reply: the substring of the string value stored at key, determined by the offsets start and end (both are inclusive).

</details>

## Keys

<details>
<summary>EXISTS</summary>

> Returns if key exists.  
The user should be aware that if the same existing key is mentioned in the arguments multiple times, it will be counted multiple times. So if somekey exists, EXISTS somekey somekey will return 2.

### Syntax

```
EXISTS key [key ...]
```

### Reply

- Integer reply: the number of keys that exist from those specified as arguments.

</details>

<details>
<summary>TTL</summary>

> Returns the remaining time to live of a key that has a timeout. This introspection capability allows a client to check how many seconds a given key will continue to be part of the dataset.

### Syntax

```
TTL key
```

### Reply

- Integer reply: TTL in seconds.
- Integer reply: -1 if the key exists but has no associated expiration.
- Integer reply: -2 if the key does not exist.

</details>

<details>
<summary>DEL</summary>

> Removes the specified keys. A key is ignored if it does not exist.

### Syntax

```
DEL key [key ...]
```

### Reply

- Integer reply: the number of keys that were removed.

</details>

## Misc

<details>
<summary>DBSIZE</summary>

> Return the number of keys in rdss.

### Syntax

```
DBSIZE
```

### Reply

- Integer reply: the number of keys in rdss.

</details>

<details>
<summary>INFO</summary>

> The INFO command returns information and statistics about the server in a format that is simple to parse by computers and easy to read by humans.

### Syntax

```
INFO [section [section ...]]
```

### Options

The optional parameter can be used to select a specific section of information:

- server: General information about the rdss server
- clients: Client connections section
- memory: Memory consumption related information
- stats: General statistics
- keyspace: Database related statistics

### Fields of each section

#### server

- multiplexing_api
- process_id
- tcp_port
- server_time_usec
- uptime_in_seconds
- uptime_in_days
- hz
- configured_hz

#### clients

- connected_clients
- maxclients
- client_recent_max_input_buffer
- client_recent_max_output_buffer

#### memory

- used_memory
- used_memory_peak
- total_system_memory

#### stats

- total_connections_received
- total_commands_processed
- total_net_input_bytes
- total_net_output_bytes
- rejected_connections
- expired_keys
- expired_stale_perc
- expired_time_cap_reached_count
- expire_cycle_cpu_milliseconds
- evicted_keys

#### keyspace

- keys:expires

### Reply

- Bulk string reply: a map of info fields, one field per line in the form of <field>:<value> where the value can be a comma separated map like <key>=<val>. Also contains section header lines starting with # and blank lines.

</details>
