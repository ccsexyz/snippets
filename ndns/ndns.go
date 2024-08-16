package main

import (
	"encoding/json"
	"flag"
	"io/ioutil"
	"log"
	"net"
	"sync"
	"time"

	"github.com/fsnotify/fsnotify"
	"github.com/miekg/dns"
)

type record struct {
	Host    string   `json:"host"`
	IPs     []string `json:"ips"`
	CName   string   `json:"cname"`
	TTL     int      `json:"ttl"`
	V6TTL   int      `json:"v6_ttl"`
	SleepMs int      `json:"sleep_ms"`
	TC      bool     `json:"tc"`
}

var (
	file     string
	tcp      bool
	udp      bool
	addr     string
	autoload bool

	recordLock sync.Mutex
	recordMap  map[string]*record
)

func init() {
	flag.StringVar(&file, "f", "record.json", "record file")
	flag.BoolVar(&tcp, "t", false, "listen tcp")
	flag.BoolVar(&udp, "u", true, "listen udp")
	flag.StringVar(&addr, "l", ":53", "listen address")
	flag.BoolVar(&autoload, "a", true, "auto reload record file")

	recordMap = make(map[string]*record)
}

func lookUpRecord(k string) (r *record, ok bool) {
	recordLock.Lock()
	r, ok = recordMap[k]
	recordLock.Unlock()
	return
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

	r, ok := lookUpRecord(domain)
	if !ok {
		log.Println("domain", domain, "not found")
		return
	}

	v4ttl := r.TTL
	v6ttl := r.V6TTL
	if v6ttl == 0 {
		v6ttl = v4ttl
	}

	for _, ipstr := range r.IPs {
		ip := net.ParseIP(ipstr)

		if ip == nil || ip.To16() == nil {
			continue
		}

		if ip.To4() == nil && is6 {
			msg.Answer = append(msg.Answer, &dns.AAAA{
				Hdr:  dns.RR_Header{Name: domain, Rrtype: dns.TypeAAAA, Class: dns.ClassINET, Ttl: uint32(v6ttl)},
				AAAA: ip,
			})
		} else if ip.To4() != nil && is4 {
			msg.Answer = append(msg.Answer, &dns.A{
				Hdr: dns.RR_Header{Name: domain, Rrtype: dns.TypeA, Class: dns.ClassINET, Ttl: uint32(v4ttl)},
				A:   ip,
			})
		}
	}

	if len(r.CName) > 0 {
		msg.Answer = append(msg.Answer, &dns.CNAME{
			Hdr:    dns.RR_Header{Name: domain, Rrtype: dns.TypeCNAME, Class: dns.ClassINET, Ttl: uint32(r.TTL)},
			Target: r.CName + ".",
		})
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

func loadFile() error {
	records, err := readRecords(file)
	if err != nil {
		return err
	}

	recordLock.Lock()
	defer recordLock.Unlock()

	recordMap = make(map[string]*record)

	for _, v := range records {
		recordMap[v.Host+"."] = v
	}

	return nil
}

func updater() {
	watcher, err := fsnotify.NewWatcher()
	if err != nil {
		log.Fatal(err)
	}
	defer watcher.Close()

	err = watcher.Add(file)
	if err != nil {
		log.Fatal(err)
	}
	defer watcher.Remove(file)

	for event := range watcher.Events {
		log.Println("updater go event", event)
		err = loadFile()
		if err != nil {
			log.Println("try to load file, but failed", err)
		}
		watcher.Remove(file)
		watcher.Add(file)
	}
}

func main() {
	flag.Parse()

	if err := loadFile(); err != nil {
		log.Fatal(err)
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

	go updater()

	wg.Wait()
}
