package main

import (
	"flag"
	"fmt"
	"net/http"
)

var (
	directory string
	port      int
)

func init() {
	flag.StringVar(&directory, "dir", "/", "directory")
	flag.IntVar(&port, "port", 8080, "listen port")
}

func main() {
	flag.Parse()

	http.Handle("/", http.FileServer(http.Dir(directory)))

	fmt.Println("Start to Serve on port:", port)
	http.ListenAndServe(fmt.Sprintf(":%d", port), nil)
}
