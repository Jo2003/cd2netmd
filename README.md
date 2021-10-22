# cd2netmd
A command line tool for Windows used to write audio tracks from CD to NetMD device. 
It uses a fork of [go-netmd-cli](https://github.com/Jo2003/go-netmd-cli) and [go-netmd-lib](https://github.com/Jo2003/go-netmd-lib) 
to write to NetMD device. 

I have heavily used CodeProject to ease the implementation of missing parts (CD ripping, parameter parsing, ...).

cd2netmd will grab the CD info from gnudb.org to name the titles on the disc. 

cd2netmd supports following command line arguments:
```
Usage: C:\msys64\home\Jo2003\src\cd2netmd\cd2netmd.exe [options]
  -v --verbose [default: (unset)]
      Do verbose output.
  -h --help [default: (unset)]
      Print help screen and exits program.
  -n --no-delete [default: (unset)]
      Do not erase MD before writing. In that case also disc title isn't
      changed.
  -d --drive [default: -]
      Drive letter of CD drive to use (w/o colon). If not given first drive
      found will be used.
  -e --encode [default: sp]
      Encoding for NetMD transfer. Default is 'sp'. MDLP modi (lp2, lp4) are
      only supported on SHARP IM-DR4x0, Sony MDS-JB980, Sony MDS-JB780
```

To use this tool you have to install the WebUSB driver using a tool named [Zadig](https://zadig.akeo.ie/) first.

Please keep in mind that this tool is in a very early stage. Things might work ... or even not work.

There is no plan to port this dirty stuff to Linux or Mac. There you have a whole lot of command line tools you easely can
combine in a bash script to do these things done in this pease of code.