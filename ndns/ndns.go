package main

import (
	"encoding/json"
	"flag"
	"io/ioutil"
	"log"
	"net"
	"sync"
	"time"

	"github.com/miekg/dns"
)

type record struct {
	Host    string   `json:"host"`
	IPs     []string `json:"ips"`
	TTL     int      `json:"ttl"`
	SleepMs int      `json:"sleep_ms"`
	TC      bool     `json:"tc"`
}

var (
	file string
	tcp  bool
	udp  bool
	addr string

	recordMap map[string]*record
)

func init() {
	flag.StringVar(&file, "f", "record.json", "record file")
	flag.BoolVar(&tcp, "t", false, "listen tcp")
	flag.BoolVar(&udp, "u", true, "listen udp")
	flag.StringVar(&addr, "l", ":53", "listen address")

	recordMap = make(map[string]*record)
}

type handler struct {
	isTcpServer bool
}

func (this *handler) packAnswers(msg *dns.Msg, qtype uint16, domain string) {
	is4 := (qtype == dns.TypeA)
	is6 := (qtype == dns.TypeAAAA)

	if !is4 && !is6 {
		log.Println("not A or AAAA request")
		return
	}

	r, ok := recordMap[domain]
	if !ok {
		log.Println("domain", domain, "not found")
		return
	}

	for _, ipstr := range r.IPs {
		ip := net.ParseIP(ipstr)

		if ip == nil || ip.To16() == nil {
			continue
		}

		if ip.To4() == nil && is6 {
			msg.Answer = append(msg.Answer, &dns.AAAA{
				Hdr:  dns.RR_Header{Name: domain, Rrtype: dns.TypeAAAA, Class: dns.ClassINET, Ttl: uint32(r.TTL)},
				AAAA: ip,
			})
		} else if ip.To4() != nil && is4 {
			msg.Answer = append(msg.Answer, &dns.A{
				Hdr: dns.RR_Header{Name: domain, Rrtype: dns.TypeA, Class: dns.ClassINET, Ttl: uint32(r.TTL)},
				A:   ip,
			})
		}
	}

	if r.SleepMs > 0 {
		time.Sleep(time.Duration(r.SleepMs) * time.Millisecond)
	}

	if r.TC && this.isTcpServer == false {
		msg.Truncated = true
	}
}

func (this *handler) ServeDNS(w dns.ResponseWriter, r *dns.Msg) {
	msg := dns.Msg{}
	msg.SetReply(r)

	qtype := r.Question[0].Qtype
	domain := msg.Question[0].Name

	this.packAnswers(&msg, qtype, domain)

	w.WriteMsg(&msg)
}

func readRecords(path string) (records []*record, err error) {
	bytes, err := ioutil.ReadFile(path)
	if err != nil {
		return
	}

	err = json.Unmarshal(bytes, &records)
	return
}

func runTcpServer() {
	srv := &dns.Server{Addr: addr, Net: "tcp"}
	srv.Handler = &handler{isTcpServer: true}
	if err := srv.ListenAndServe(); err != nil {
		log.Fatalf("Failed to set tcp listener %s\n", err.Error())
	}
}

func runUdpServer() {
	srv := &dns.Server{Addr: addr, Net: "udp"}
	srv.Handler = &handler{}
	if err := srv.ListenAndServe(); err != nil {
		log.Fatalf("Failed to set udp listener %s\n", err.Error())
	}
}

func main() {
	flag.Parse()

	records, err := readRecords(file)
	if err != nil {
		log.Fatal(err)
	}

	for _, v := range records {
		recordMap[v.Host+"."] = v
	}

	var wg sync.WaitGroup

	if tcp {
		wg.Add(1)
		go func() {
			runTcpServer()
			wg.Done()
		}()
	}

	if udp {
		wg.Add(1)
		go func() {
			runUdpServer()
			wg.Done()
		}()
	}

	wg.Wait()
}
