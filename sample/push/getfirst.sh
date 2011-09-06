
channel='ch_teste'
cnt=${1:-3}

curl -v "http://localhost:8080/broadcast/sub?ch=$channel&m=$cnt&s=F"
