#!/bin/bash -e

id=${1:-1}
channel=${2:-'ch_teste'}
url="http://localhost:8080/broadcast/sub?ch=$channel&m=$id&s=M"
c=${3:-1}

for ((i=0; i<$c; i++))
do
	curl -m 600 -v "$url" &
done

wait
#ab -c10 -n50 -t 600 "$url"
