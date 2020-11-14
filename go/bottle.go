package main

import (
	"bufio"
	"fmt"
	"log"
	"net"
	"strings"
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

func main() {
	c, err := net.Dial("tcp", "irc.freenode.net:6667")
	if err != nil {
		log.Fatal(err)
	}

	nick := "bottle"
	channel := "#bottlechan"
	fmt.Fprintf(c, "nick %s\r\n", nick)
	fmt.Fprintf(c, "user %s %s %s :%s\r\n", "username", "localname", "remotename", "fancy description")
	fmt.Fprintf(c, "join %s\r\n", channel)

	handlers := make(map[string]func(string, []string))
	ignore := func(string, []string) {}
	ignore_channel := func(cmd string, args []string) {
		if len(args) < 1 || args[0] != channel {
			log.Fatalf("unexpected channel, cmd '%s', args %v", cmd, args)
		}
	}
	ignore_nick_channel := func(cmd string, args []string) {
		if len(args) < 2 || args[0] != nick || args[1] != channel {
			log.Fatalf("unexpected channel, cmd '%s', args %v", cmd, args)
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

	handlers["PING"] = func(_ string, args []string) {
		if len(args) != 1 {
			log.Fatalf("invalid ping args, %v", args)
		}
		log.Printf("got ping, sending pong, '%s'", args[0])
		fmt.Fprintf(c, "pong :%s\r\n", args[0])
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
			handler(cmd, args)
			continue
		}

		log.Printf("%24s %s", prefix, text)
	}
	if err := connreader.Err(); err != nil {
		log.Fatal(err)
	}
}
