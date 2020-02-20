#!/bin/sh

[ -x "$(command -v tac)" ] || function tac { sed '1!G;h;$!d' "$@"; }

function opkextract() {
	opkpath="$1"

	[ ! -f "$opkpath" ] && echo "File not found" && exit 1;

	opkname=$(basename "$opkpath")
	opkname="${opkname%%.*}"

	mkdir -p /tmp/.opk
	unsquashfs -d /tmp/.opk -f "$opkpath" -e "*.desktop" &> /dev/null

	for f in /tmp/.opk/*.desktop; do
		desktopname=$(basename "$f")
		desktopname="${desktopname%%.*}"
		category=$(grep -i "^[[:space:]]*\(Categories\)" "$f" | sed -e 's/^[[:space:]]*Categories[[:space:]]*=[[:space:]]*\([0-9A-Z]\+\).*/\1/gi')
		[ -z "$category" ] && category="applications"

		echo "Updating '$category/${opkname}.${desktopname}.lnk'... "
		mkdir -p "$HOME/apps/gmenu2x/sections/$category"
		opklink="$HOME/apps/gmenu2x/sections/$category/${opkname}.${desktopname}.lnk"

		# icon=$(grep "^[[:space:]]*Icon" "$f" | cut -f2 -d"=" | xargs).png
		# unsquashfs -d /tmp/.opk -f "$opkpath" -e "$icon" &> /dev/null
		# cp "/tmp/.opk/$icon" "$HOME/.cache/$opkname.png"

		# exec=$(grep "^[[:space:]]*Exec" "$f" | cut -f2 -d"=" | xargs)
		# unsquashfs -d /tmp/.opk -f "$opkpath" -e "$icon" &> /dev/null
		# cp "/tmp/.opk/$icon" "$HOME/.cache/$opkname.png"

		echo "exec=$opkpath" >> "$opklink"

		grep -i "^[[:space:]]*\(Exec\|Name\|Comment\|Icon\|X-OD-Manual\|X-OD-Selector\|X-OD-Filter\|X-OD-Alias\)" "$f" | \
		sed \
			-e 's/^[[:space:]]*Exec[[:space:]]*=/params=/gi' \
		    -e 's/^[[:space:]]*Icon[[:space:]]*=[[:space:]]*\(.\+\)/icon=\1.png/gi' \
		    -e 's/^[[:space:]]*Name[[:space:]]*=/title=/gi' \
		    -e 's/^[[:space:]]*Comment[[:space:]]*=/description=/gi' \
		    -e 's/^[[:space:]]*X-OD-Manual[[:space:]]*=/manual=/gi' \
		    -e 's/^[[:space:]]*X-OD-Selector[[:space:]]*=/selectordir=/gi' \
		    -e 's/^[[:space:]]*X-OD-Filter[[:space:]]*=/selectorfilter=/gi' \
		    -e 's/^[[:space:]]*X-OD-Alias[[:space:]]*=/aliases=/gi' \
		    >> "$opklink"
		echo "clock=600" >> "$opklink"

		# tac "$opklink" | sort -u -t "=" -k 1,1 -o "$opklink"
		sort -u -t "=" -k 1,1 -o "$opklink" "$opklink"
		# echo "[OK]"
	done
	rm -rf /tmp/.opk
}

if [ ! -z "$1" ]; then
	opkextract "$1"
	exit
fi

echo "Updating OPK packages..."

# rm -rf "$HOME/.cache"
# mkdir -p "$HOME/.cache"

# find "." -maxdepth 2 -mindepth 2 -name "*.opk" -type f | while read opkpath; do
find "$HOME" /media/mmcblk* -maxdepth 2 -mindepth 1 -iname "*.opk" -type f 2> /dev/null | while read opkpath; do
	opkextract "$opkpath"
done
