
channel='ch_teste'
id=${1:-1}

curl -m 600  -v "http://localhost:8080/broadcast/sub?ch=$channel&m=$id&s=M"
#ab -c50 -n50 -t 600 "http://localhost:8080/broadcast/sub?ch=$channel&m=$id&s=M"
