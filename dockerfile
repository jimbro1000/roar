FROM xroar/base:12.7
WORKDIR /usr/local
COPY . /usr/local/xroar

CMD cd /usr/local/xroar && /usr/local/xroar/containerbuildapp.sh
