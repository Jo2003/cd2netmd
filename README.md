# cd2netmd
A command line tool for Windows used to write audio tracks from CD to NetMD device. 
It uses a fork of [go-netmd-cli](https://github.com/Jo2003/go-netmd-cli) and [go-netmd-lib](https://github.com/Jo2003/go-netmd-lib) 
to write to NetMD device. 

External Atrac3 encoding is done using [atracdenc](https://github.com/dcherednik/atracdenc).

I have heavily used CodeProject to ease the implementation of missing parts (CD ripping, parameter parsing, ...).

cd2netmd will grab the CD info from gnudb.org to name the titles on the disc. 

cd2netmd supports following command line arguments:
```
Usage: cd2netmd [options]
  -v --verbose [default: false]
      Does verbose output.
  -h --help [default: false]
      Prints help screen and exits program.
  -a --append [default: false]
      Don't erase MD before writing, but append tracks instead. MDs discs title will not be changed.
  -i --ignore-cddb [default: false]
      Ignore CDDB lookup errors.
  -d --drive-letter [default: -]
      Drive letter of CD drive to use (w/o colon). If not given first CD drive found will be used.
  -e --encode [default: sp]
      On-the-fly encoding mode on NetMD device while transfer. Default is 'sp'. Note: MDLP modi
      (lp2, lp4) are supported only on SHARP IM-DR4x0, Sony MDS-JB980, Sony MDS-JB780.
  -x --ext-encode [default: no]
      External encoding before NetMD transfer. Default is 'no'. MDLP modi (lp2, lp4) are supported.
      Note: lp4 sounds horrible. Use it - if any - only for audio books! In case your NetMD device
      supports On-the-fly encoding, better use -e option instead!
```

> Note:
> Starting the program without a parameter and an CD Audio in your CD drive will 
> start the ripping process and will **ERASE** the content of your MD in you NetMD drive!

To use this tool you have to install the WebUSB driver using a tool named [Zadig](https://zadig.akeo.ie/) first.

Please keep in mind that this tool is in a early stage. Things might work ... or even not work.

There is no plan to port this dirty stuff to Linux or Mac. There you have a whole lot of command line tools you easely can
combine in a bash script to do these things done in this pease of code.

## Examples
* `cd2netmd` extracts the CD in first drive, erases the MD, titles the MD, transfers all audio tracks to NetMD using SP mode, titles the tracks an MD.
* `cd2netmd -e lp2` same as above, but uses NetMD devices on-the-fly lp2 encoder to store the tracks in lp2 format on MD.
* `cd2netmd -x lp2` same as above, but uses the external ATRAC3 encoder (in case your device doesn't support on-the-fly encoding).
* `cd2netmd -a -x lp2` same as above, but doesn't erase MD. New tracks will be appended to MD. Disc title will not be changed.
* `cd2netmd -d f` uses CD drive f: