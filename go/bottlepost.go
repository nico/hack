package main

// This program joins an IRC channel and then listens to POST requests
// and posts messages posted to it to the channel.
// curl -d "msg=say&msg=foo&msg=and" -X POST 'localhost:8080/?msg=bar'

import (
	"bufio"
	"flag"
	"fmt"
	"log"
	"math/rand"
	"net"
	"net/http"
	"strings"
	"time"
)

const (
	RPL_WELCOME       = "001"
	RPL_YOURHOST      = "002"
	RPL_CREATED       = "003"
	RPL_MYINFO        = "004"
	RPL_ISUPPORT      = "005"
	RPL_STATSDLINE    = "250"
	RPL_LUSERCLIENT   = "251"
	RPL_LUSEROP       = "252"
	RPL_LUSERKNOWN    = "253"
	RPL_LUSERCHANNELS = "254"
	RPL_LOCALUSERS    = "265"
	RPL_GLOBALUSERS   = "266"
	RPL_LUSERME       = "255"

	RPL_MOTDSTART = "375"
	RPL_MOTD      = "372"
	RPL_ENDOFMOTD = "376"

	RPL_TOPIC        = "332"
	RPL_TOPICWHOTIME = "333"
	RPL_NAMREPLY     = "353"
	RPL_ENDOFNAMES   = "366"
	RPL_CHANNEL_URL  = "328"

	RPL_HOSTHIDDEN = "396"
)

func post(w http.ResponseWriter, req *http.Request, send func(string)) {
	if req.URL.Path != "/" {
		http.Error(w, "404 Not Found", http.StatusNotFound)
		return
	}
	if req.Method != "POST" {
		http.Error(w, "405 Method Not Allowed", http.StatusMethodNotAllowed)
		return
	}
	if err := req.ParseForm(); err != nil {
		log.Print(err)
		http.Error(w, "400 Bad Request", http.StatusBadRequest)
		return
	}
	var msgs []string
	for name, values := range req.Form {
		if name != "msg" {
			log.Print("invalid query param %V", name)
			http.Error(w, "400 Bad Request", http.StatusMethodNotAllowed)
			return
		}
		msgs = values
	}
	for _, h := range msgs {
		send(h)
	}
}

func on_channel_joined(port string, send func(string)) {
	http.HandleFunc("/", func(w http.ResponseWriter, req *http.Request) { post(w, req, send) })
	log.Fatal(http.ListenAndServe(port, nil))
}

func main() {
	var irc_server_flag = flag.String("irc_server", "irc.freenode.net:6667", "irc network to join")
	var http_port_flag = flag.String("port", ":8080", "port to serve at")
	var nick_flag = flag.String("irc_nick", "bottle", "irc nickname")
	var channel_flag = flag.String("irc_channel", "#bottlechan", "irc channel to join")
	flag.Parse()

	c, err := net.Dial("tcp", *irc_server_flag)
	if err != nil {
		log.Fatal(err)
	}

	nick := *nick_flag
	channel := *channel_flag

	// Username and description are shown by /whois.
	username := "username"
	description := "an almost sentient bot"

	fmt.Fprintf(c, "nick %s\r\n", nick)
	fmt.Fprintf(c, "user %s %s %s :%s\r\n", username, "localname", "remotename", description)
	fmt.Fprintf(c, "join %s\r\n", channel)

	has_joined_channel := false

	handlers := make(map[string]func(string, string, []string))
	ignore := func(string, string, []string) {}
	ignore_channel := func(_ string, cmd string, args []string) {
		if len(args) < 1 || args[0] != channel {
			log.Fatalf("unexpected channel, cmd '%s', args %v", cmd, args)
		}
		if !has_joined_channel {
			go on_channel_joined(*http_port_flag, func(m string) { fmt.Fprintf(c, "privmsg %s :%s\n", channel, m) })
			has_joined_channel = true
		}
	}
	ignore_nick_channel := func(_ string, cmd string, args []string) {
		if len(args) < 2 || args[0] != nick || args[1] != channel {
			log.Fatalf("unexpected channel, cmd '%s', args %v", cmd, args)
		}
		if !has_joined_channel {
			go on_channel_joined(*http_port_flag, func(m string) { fmt.Fprintf(c, "privmsg %s :%s\n", channel, m) })
			has_joined_channel = true
		}
	}

	handlers["NOTICE"] = ignore
	handlers[RPL_WELCOME] = ignore
	handlers[RPL_YOURHOST] = ignore
	handlers[RPL_CREATED] = ignore
	handlers[RPL_MYINFO] = ignore
	handlers[RPL_ISUPPORT] = ignore
	handlers[RPL_LUSERCLIENT] = ignore
	handlers[RPL_LUSEROP] = ignore
	handlers[RPL_LUSERKNOWN] = ignore
	handlers[RPL_LUSERCHANNELS] = ignore
	handlers[RPL_LUSERME] = ignore
	handlers[RPL_LOCALUSERS] = ignore
	handlers[RPL_GLOBALUSERS] = ignore
	handlers[RPL_STATSDLINE] = ignore
	handlers[RPL_MOTDSTART] = ignore
	handlers[RPL_MOTD] = ignore
	handlers[RPL_ENDOFMOTD] = ignore
	handlers["MODE"] = ignore
	handlers[RPL_HOSTHIDDEN] = ignore
	handlers[RPL_TOPIC] = ignore_nick_channel
	handlers[RPL_TOPICWHOTIME] = ignore_nick_channel
	handlers[RPL_NAMREPLY] = ignore
	handlers[RPL_ENDOFNAMES] = ignore_nick_channel
	handlers[RPL_CHANNEL_URL] = ignore_nick_channel
	handlers["JOIN"] = ignore_channel
	handlers["PART"] = ignore_channel

	handlers["PING"] = func(_ string, _ string, args []string) {
		if len(args) != 1 {
			log.Fatalf("invalid ping args, %v", args)
		}
		log.Printf("got ping, sending pong, '%s'", args[0])
		fmt.Fprintf(c, "pong :%s\r\n", args[0])
	}

	handlers["PRIVMSG"] = func(prefix string, _ string, args []string) {
		if len(args) != 2 {
			log.Printf("privmsg bad args %v from %s", args, prefix)
			return
		}
		if args[0] != channel {
			// FIXME: Support whisper-back to whisper?
			log.Printf("got msg to '%s': %v from %s", args[0], args, prefix)
			return
		}
		msg := args[1]
		if len(msg) < len(nick)+2 ||
			msg[:len(nick)] != nick ||
			(msg[len(nick):len(nick)+2] != ": " && msg[len(nick):len(nick)+2] != ", ") {
			return
		}
		msg = msg[len(nick)+2:]
		if msg == "dance" {
			go func() {
				const art = `¯\_(ツ)_/¯
---(ツ)_/¯
_/¯(ツ)_/¯
_/¯(ツ)---
_/¯(ツ)¯\_`
				lines := strings.Split(art, "\n")
				for _, line := range lines {
					fmt.Fprintf(c, "privmsg %s :%s\n", channel, line)
					time.Sleep(1 * time.Second)
				}
				for i := len(lines) - 2; i >= 0; i-- {
					fmt.Fprintf(c, "privmsg %s :%s\n", channel, lines[i])
					time.Sleep(1 * time.Second)
				}
			}()
		} else {
			replies := []string{`ಠ_ಠ`, `☜(°O°☜)`, `¯\_(ツ)_/¯`, `what`, `(⚆_⚆)`, `( ͡°_ʖ ͡°)`, `(¬_¬)`, `( ͡ᵔ ͜ʖ ͡ᵔ )`,
				`ლ(ಠ_ಠლ)`, "(´･_･`)", `ლ(ಠ益ಠლ)`, `(┛ಠ_ಠ)┛彡┻━┻`, `☜(ﾟヮﾟ☜)`, `(☞ﾟヮﾟ)☞`, `ヽ༼ʘ̚ل͜ʘ̚༽ﾉ`, `(－‸ლ)`, `(˚ㄥ_˚)`, `(◕‿◕✿)`, `ʕ ᵔᴥᵔ ʔ`}
			fmt.Fprintf(c, "privmsg %s :%s\n", channel, replies[rand.Intn(len(replies))])
		}

	}

	connreader := bufio.NewScanner(c)
	for connreader.Scan() {
		text := connreader.Text()
		if text == "" {
			continue
		}

		// Parse command.
		prefix := ""
		if text[0] == ':' {
			if i := strings.Index(text, " "); i > -1 {
				prefix = text[1:i]
				text = text[i+1 : len(text)]
				if text == "" {
					log.Fatal("no data after prefix in '%s'", text)
				}
			} else {
				log.Fatal("invalid prefix in '%s'", text)
			}
		}

		split := strings.SplitN(text, " :", 2)
		args := strings.Split(split[0], " ")
		cmd := strings.ToUpper(args[0])
		args = args[1:]
		if len(split) > 1 {
			args = append(args, split[1])
		}

		// Handle command.
		if handler, ok := handlers[cmd]; ok {
			handler(prefix, cmd, args)
			continue
		}

		log.Printf("%24s %s", prefix, text)
	}
	if err := connreader.Err(); err != nil {
		log.Fatal(err)
	}
}
