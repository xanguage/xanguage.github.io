#!/usr/bin/env bash

# See UNLICENSE file for copyright and license details.

DICT_PATH="dictionary"

wordlist="const wordlist = ["
desclist="const desclist = [["
deflist="const deflist = [["
taglist="const taglist = [[["

for w in $DICT_PATH/*; do
	word="$(basename $w)"
	if [[ $word == *"--"* ]]; then
		word="$(awk -F'--' '{print $2}' <<< $word)"
	fi
	wordlist+="\"${word}\", "
	while read -r d; do
		desc="$(cut -f2 -d'|' <<< $d)"
		desclist+="\"$(sed 's/"/\\"/g' <<< $desc)\", "
		def="$(cut -f1 -d'|' <<< $d)"
		deflist+="\"$(sed 's/"/\\"/g' <<< $def)\", "

		tags="$(cut -f3 -d'|' <<< $d)"
		IFS=' ' read -ra tagarr <<< "$tags"
		for tag in "${tagarr[@]}"; do
			taglist+="\"${tag}\", "
		done
		taglist="$(sed 's/\(.*\), /\1/' <<< $taglist)], ["
	done < $w
	desclist="$(sed 's/\(.*\), /\1/' <<< $desclist)], ["
	deflist="$(sed 's/\(.*\), /\1/' <<< $deflist)], ["
	taglist="$(sed 's/\(.*\), \[/\1/' <<< $taglist)], [["
done

wordlist="$(sed 's/\(.*\), /\1/' <<< $wordlist)];"
desclist="$(sed 's/\(.*\), \[/\1/' <<< $desclist)];"
deflist="$(sed 's/\(.*\), \[/\1/' <<< $deflist)];"
taglist="$(sed 's/\(.*\), \[\[/\1/' <<< $taglist)];"

echo $wordlist
echo $desclist
echo $deflist
echo $taglist
