# netdubhd-upload
Upload m2ts video to TOSHIBA HDD recorder via Net Dubbing HD.  
Tested on Windows 7/Linux with RD-X9 in May 2010.

## HowToUse
1. Create .req file for metadata (video title, description, date, genre and so on)  
`mk-createreq <m2ts image file>`

2. Upload image to TOSHIBA HDD recorder  
`netdubhd-upload <dest addr[:port][/url]> <m2ts image file> [dlna_org_pn]`

Ex)  
`netdubhd-upload 192.168.1.128 video.m2ts`  
`netdubhd-upload 192.168.1.129:55247/dms/control/ContentDirectory bill_pts.m2ts`

## Compile  
`gcc -o netdubhd-upload upload_image.c ts-filter.c`  
`gcc -o mk-createreq mk-createreq.c arib_string.c`

### Note
For MinGW build, add compile option `-lws2_32` last.
