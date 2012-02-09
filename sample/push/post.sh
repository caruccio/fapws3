
date=`date +%s`
id=${1:-1}
channel=${2:-'ch_teste'}
to=${3:-60}
content="This is Spartaaaaaaa id=$id!!!"
post_data="
{
 \"version\":\"1\",
 \"operation\":\"INSERT\",
 \"channelCode\":\"$channel\",
 \"reference\":\"0\",
 \"payload\":\"$content\",
 \"realtimeId\":\"$id\",
 \"dtCreated\":\"$date\"
}"

post_data="*** PAYLOAD $id/$channel ***"

curl -v "http://localhost:8080/broadcast/pub" -d "ch=$channel&m=$id&t=$date&to=$to&rt_message=$post_data"
