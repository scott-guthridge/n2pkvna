#!/bin/sh
list=`
    curl -s "https://api.github.com/repos/scott-guthridge/libvna/releases/latest" |
	grep "browser_download_url" | cut -d '"' -f 4 |
    while read x; do
	case "$x" in
	*.deb)
	    curl -L -O "$x"
	    base=\`basename "$x"\`
	    echo -n " ./$base"
	    ;;
	*)
	    ;;
	esac
    done`
echo "list: $list"
exec sudo apt install -y $list
