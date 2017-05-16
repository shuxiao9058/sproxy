#!/bin/bash

CURDIR=$(pwd)
DESTDIR=/usr/local/ngx_stream_shadowsocks
BUILDROOT=$(pwd)/buildroot/

build_what=modules

if [ $# = 1 -a "$1" = "all" ]; then
    build_what=
fi


NGINX_VERSION=1.12.0
NGINX=nginx-${NGINX_VERSION}

rm -fr $BUILDROOT/*

cp -af $NGINX $BUILDROOT/
cp -af ngx_stream_shadowsocks_module $BUILDROOT

cd $BUILDROOT/nginx-${NGINX_VERSION}/

CFLAGS="-g -O0" ./configure --prefix=${DESTDIR} \
    --without-http \
    --with-stream \
    --with-stream_ssl_module \
    --add-dynamic-module=../ngx_stream_shadowsocks_module/ || exit -1

make -j 2 $build_what || exit -1
install -d ${DESTDIR}/{logs,conf,sbin,modules}

${DESTDIR}/sbin/nginx -s stop
if [ "$build_what" = "" ]; then
    install -m0755 objs/nginx ${DESTDIR}/sbin/nginx
fi
echo install -m0755 objs/ngx_stream_shadowsocks_module.so ${DESTDIR}/modules
install -m0755 objs/ngx_stream_shadowsocks_module.so ${DESTDIR}/modules
cd ${CURDIR}
install -m0644 nginx.conf  ${DESTDIR}/conf/nginx.conf
${DESTDIR}/sbin/nginx


