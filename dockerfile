FROM xroar/base:12.7
WORKDIR /usr/local

CMD cd /usr/local/xroar && /usr/local/xroar/containerbuildapp.sh
