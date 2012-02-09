
channel="${1:-ch_teste}"
cnt=${2:-3}

curl -v "http://localhost:8080/broadcast/sub?ch=$channel&m=$cnt&s=L"
