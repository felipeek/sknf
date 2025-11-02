package util

import (
	"io"
	"os"
)

func Copy(srcpath, dstpath string, mode os.FileMode) (err error) {
	r, err := os.Open(srcpath)
	if err != nil {
		return err
	}
	defer r.Close()

	w, err := os.Create(dstpath)
	if err != nil {
		return err
	}

	defer func() {
		// Report the error, if any, from Close, but do so
		// only if there isn't already an outgoing error.
		if c := w.Close(); err == nil {
			err = c
		}
	}()

	_, err = io.Copy(w, r)
	if err != nil {
		return err
	}

	err = w.Chmod(mode)
	return err
}

func ReadFileToString(path string) (string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	return string(data), nil
}

func WriteStringToFile(path, content string) error {
	return os.WriteFile(path, []byte(content), 0644)
}
