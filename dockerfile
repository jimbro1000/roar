FROM debian:12.7
WORKDIR /usr/local/xroar

RUN apt-get update && apt-get upgrade -y && apt-get install -y build-essential libgtk-3-dev libpulse-dev libpng-dev

CMD cd /usr/local/xroar && ./containerbuildapp.sh
