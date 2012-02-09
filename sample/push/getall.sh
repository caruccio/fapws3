
channel="${1:-ch_teste}"

curl -v "http://localhost:8080/broadcast/sub?ch=$channel&s=A"
