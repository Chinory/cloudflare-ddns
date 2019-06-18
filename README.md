# cfddns	

A lightweight and easy-to-configure Cloudflare DDNS client written by pure C with libcurl

- Use a single file to configure and cache, while saving text structures and comments
- Support multiple accounts, domains, records, and is not limited to updating A, AAAA records
- Get new values via curl and automatically update the corresponding DNS record

## Configuration

```
# comment
ipv4? ipv4.icanhazip.com
ipv6? ipv6.icanhazip.com # comment
user1@example.com: 0a01392b47df17f33fff431b1f6f762f94bd9
  example1.com/
    A @   ipv4
    A www ipv4
```

The effect line can be like this:

- `var? url [value]`

Get latest value from the `url` , update the `value` and bind to the `var`. Cannot be rebinded.

- `email: apikey`

Set `email` and `apikey`, must provide `apikey` otherwise `email` will be earsed too.

- `zone_name/ [zone_id]`

Set `zone_name` . The `zone_id` will be cached after this. Do not check if `zone_id` is valid.

- `type name var [record_id][!]`

If `var` changed, update this record. If update failed, try to regain the `record_id` provided by the file and retry, and if failed again, `!` will be appended to indicate force update next time.

Indentation and order are not necessary, because each line is executed sequentially as a separate statement, so you need to make sure there are variables before updating the record.

## Usage

```
$ cfddns /path/to/cfddns.conf
# comment
ipv4? ipv4.icanhazip.com 153.68.35.833 #changed
ipv6? ipv6.icanhazip.com # comment #request_failed
user1@example.com: 
  example1.com/ #got_zone_id
    A @   ipv4 #updated
    A www ipv6 #var_undefined 
```

The output is from the original config file and is almost same with the content write back to the config file, except no secrets like `apikey` in output and no prompt like `#changed` in config file. 

## Installation

### single executable

```
$ make
$ sudo make install
```

- /usr/local/bin/cfddns

### systemd service and timer

```
$ make
$ sudo make install-systemd
```

- /usr/local/bin/cfddns
- /etc/cfddns.conf
- /etc/systemd/system/cfddns.service
- /etc/systemd/system/cfddns.timer

## License

MIT © Chinory

