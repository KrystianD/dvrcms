DVRCMS
======

SDL based video preview tool for Chinese digital video recorders (DVR) similar to [this one](https://www.aliexpress.com/item/32828699353.html). Usually, they use port 34567.

### Compilation

```sh
git clone git@github.com:KrystianD/dvrcms --recurse-submodules
cd dvrcms
cmake .
make
```

### Usage

```shell
USAGE: 

   ./dvrcms  [-d] [--codec <CODEC>] -p <PASSWORD> -u <USER> -c <CHANNEL>
             [-P <PORT>] -H <HOST> [--] [--version] [-h]


Where: 

   -d,  --debug
     Debug mode

   --codec <CODEC>
     Codec to use (default h264)

   -p <PASSWORD>,  --password <PASSWORD>
     (required)  DVR password

   -u <USER>,  --user <USER>
     (required)  DVR user

   -c <CHANNEL>,  --channel <CHANNEL>
     (required)  DVR channel to use

   -P <PORT>,  --port <PORT>
     DVR port

   -H <HOST>,  --host <HOST>
     (required)  DVR hostname or IP address

   --,  --ignore_rest
     Ignores the rest of the labeled arguments following this flag.

   --version
     Displays version information and exits.

   -h,  --help
     Displays usage information and exits.
```