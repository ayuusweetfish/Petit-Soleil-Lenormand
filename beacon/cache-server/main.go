package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"time"
)

type ErrorResponse struct{ Status int }

func (e ErrorResponse) Error() string {
	return "ErrorResponse"
}

func handleUpload(w http.ResponseWriter, r *http.Request) []string {
	reader, err := r.MultipartReader()
	if err != nil {
		panic(ErrorResponse{400})
	}

	files := []string{}
	for {
		part, err := reader.NextPart()
		if err != nil {
			if errors.Is(err, io.EOF) {
				break
			}
			panic(ErrorResponse{400})
		}

		fileId := part.FileName()
		f, err := os.Create(filepath.Join("uploads", fileId))
		if err != nil {
			panic(err)
		}
		defer f.Close()

		buf := make([]byte, 1<<16)
		totalLen := 0
		for {
			l, errRead := part.Read(buf)
			_, err := f.Write(buf[:l])
			if err != nil {
				panic(err)
			}
			totalLen += l
			if errRead != nil {
				if errors.Is(errRead, io.EOF) {
					break
				}
				panic(ErrorResponse{400})
			}
		}
		files = append(files, fileId)
	}
	return files
}

func putHandler(w http.ResponseWriter, r *http.Request) {
	ids := handleUpload(w, r)
	fmt.Fprintln(w, strings.Join(ids, "\n"))
}

var staticFileServer http.Handler

func init() {
	staticFileServer = http.FileServer(http.Dir("uploads"))
}

func getHandler(w http.ResponseWriter, r *http.Request) {
	staticFileServer.ServeHTTP(w, r)
}

// A handler that captures panics and return the error message as 500
type errCaptureHandler struct {
	Handler http.Handler
}

func (h *errCaptureHandler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	defer func() {
		if obj := recover(); obj != nil {
			if err, ok := obj.(ErrorResponse); ok {
				http.Error(w, "", err.Status)
			} else if err, ok := obj.(error); ok {
				http.Error(w, err.Error(), 500)
			} else {
				message := fmt.Sprint(obj)
				http.Error(w, message, 500)
			}
		}
	}()
	h.Handler.ServeHTTP(w, r)
}

func ServerListen() {
	mux := http.NewServeMux()
	mux.HandleFunc("PUT /", putHandler)
	mux.HandleFunc("GET /{path...}", getHandler)

	port := 3322
	log.Printf("Listening on http://localhost:%d/\n", port)
	server := &http.Server{
		Handler:      &errCaptureHandler{Handler: mux},
		Addr:         fmt.Sprintf("localhost:%d", port),
		WriteTimeout: 15 * time.Second,
		ReadTimeout:  15 * time.Second,
		IdleTimeout:  60 * time.Second,
	}
	go func() {
		if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Print(err)
		}
	}()

	ch := make(chan os.Signal, 1)
	signal.Notify(ch, os.Interrupt)
	<-ch

	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	if err := server.Shutdown(ctx); err != nil {
		log.Print(err)
	}
	log.Print("Shutting down")
}

func main() {
	if err := os.MkdirAll(filepath.Join(".", "uploads"), 0755); err != nil {
		panic(err)
	}
	ServerListen()
}
