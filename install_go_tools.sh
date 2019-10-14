#!/bin/bash

go get -u -v golang.org/x/tools/cmd/goimports
go get -u -v golang.org/x/tools/cmd/gorename
go get -u -v github.com/sqs/goreturns
go get -u -v github.com/mdempsky/gocode
go get -u -v github.com/alecthomas/gometalinter
go get -u -v github.com/mgechev/revive
go get -u -v github.com/golangci/golangci-lint/cmd/golangci-lint
go get -u -v github.com/zmb3/gogetdoc
go get -u -v github.com/zmb3/goaddimport
go get -u -v github.com/rogpeppe/godef
go get -u -v golang.org/x/tools/cmd/guru
go get -u -v github.com/fatih/gomodifytags
if [ `uname` == "Darwin" ]
then
    go get -u -v github.com/uudashr/gopkgs/cmd/gopkgs
else
    go get -u -v github.com/tpng/gopkgs
fi
go get -u -v github.com/ramya-rao-a/go-outline
