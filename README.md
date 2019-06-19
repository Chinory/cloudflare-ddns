# cfddns	

A light and friendly Cloudflare DDNS client written by pure C with libcurl

- Use a single file to configure and cache, while saving the original indents and comments
- Support **multiple** **accounts**, **domains**, **records** and **unlimited** **types** (not limited to A, AAAA)
- Get new values from url and automatically update the corresponding DNS record

## Configuration

```shell
# comment
ipv4? ipv4.example.com
ipv4? ipv4.icanhazip.com
ipv6? ipv6.icanhazip.com # comment
text? https://example.com/text

user1@example.com: 59253b9655ed2b92d1563a2b960a8025b1ca1
  example1.com/
    A    @   ipv4 # comment
    AAAA www ipv4

user2@example.com: e23a6d4ddb46f4f9fc8cf033f064a42217388
  example2.com/ d39383d73f510f79e82788232719fc49
    AAAA dav ipv6
    TXT  @   text bad_recoird_id
```

Each line is executed sequentially as a separate statement. Effect statements can be like this:

- `var? url [content]`

Get value from the `url` , update `content` and bind to the variable `var`. Cannot be rebinded.

- `email: apikey`

Set `email` and `apikey`, must provide `apikey` otherwise `email` will be earsed too.

- `zone_name/ [zone_id]`

Set `zone_name` . The `zone_id` will be cached after this. Do not check if `zone_id` is valid.

- `type name var [record_id][!]`

If `var` changed, update this record. If update failed and the `record_id` is privided by the file, try to regain the id then try again. If failed again, a `!` will be appended to indicate force update next time.

## Usage

```shell
$ cfddns cfddns.conf
ipv4? ipv4.example.com #request_failed
ipv4? ipv4.icanhazip.com 153.68.35.233 #changed
ipv6? ipv6.icanhazip.com # comment  #request_failed
text? https://example.com/text value #changed

user1@example.com: 
  example1.com/  #got_zone_id
    A    @   ipv4 # comment #updated
    AAAA www ipv4 ! #update_failed 

user2@example.com: 
  example2.com/ 
    AAAA dav ipv6  #var_undefined 
    TXT  @   text  #updated
```

The output is from the original config file and is almost same with the content write back to the config file, except no secrets like `apikey` in output and no prompt like `#changed` in config file. 

## Dependencies

- libcurl

## Installation

### Single executable

```shell
$ make
$ sudo make install
```

- `/usr/local/bin/cfddns`
- `/etc/cfddns.conf` (no override)

### As systemd service

```shell
$ make
$ sudo make install-systemd
```

- `/usr/local/bin/cfddns`

- `/etc/cfddns.conf` (no override)

- `/etc/systemd/system/cfddns.service`

  run `journalctl -u cfddns` to check the logs

- `/etc/systemd/system/cfddns.timer`

  edit `OnUnitActiveSec=` line then run `systemd daemon-reload` to change the interval

## License

MIT © Chinory

