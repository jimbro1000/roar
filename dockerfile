FROM xroar/base:12.7
WORKDIR /usr/local

RUN apt-get update && apt-get upgrade -y 

CMD cd /usr/local/xroar && ./containerbuildapp.sh
