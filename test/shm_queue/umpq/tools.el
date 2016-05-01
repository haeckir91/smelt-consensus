(defun sk-region (start end)
  "Print number of lines and characters in the region."
  (interactive "r")
  (let ((bla (read-from-minibuffer "#define name?")))
    (save-excursion ;; Remember were we have been before
      (goto-char end)
      (insert (concat "#endif /* " bla  " */
"))
      (goto-char start)
      (insert (concat "#if defined(" bla  ")
"))
      )
    )
)

(defun sk-debug (start end)
  "Print number of lines and characters in the region."
  (interactive "r")
  (let ((bla "DEBUG"))
    (save-excursion ;; Remember were we have been before
      (goto-char end)
      (insert (concat "#endif /* " bla  " */
"))
      (goto-char start)
      (insert (concat "#if defined(" bla  ")
"))
      )
    )
)

(defun skf (start end)
  "Insert a new slide to a LaTeX beamer file"
  (interactive "r")
  (let ((title (read-from-minibuffer "Title: ")))
    (let ((subtitle (read-from-minibuffer "Subtitle: ")))
      (save-excursion ;; Remember were we have been before
	(insert (concat "
% %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
\\begin{frame}[c]{"title"}{"subtitle"}

  \\begin{itemize}
  \\item
  \\end{itemize}
\\end{frame}
"))
	)
      )
    )
  (forward-line 5) ;; We could also do a forward search to \item
  (end-of-line)
  (insert " ")
)

;; ======================================================================
(defun eth_addr ()
  "Add Systems Group address"
  (interactive)
  (insert "ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.")
)
(defun eth_copyright ()
  "Add Systems Group copyright"
  (interactive)
  (insert "Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012, 2013 ETH Zurich.")
)
(defun eth_c_header ()
  "Add Systems Group C header"
  (interactive)
  (insert "/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012, 2013 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */")
)
(defun sk_gpg_header ()
  "Add GPG header"
  (interactive)
  (insert "-*- epa-file-encrypt-to: (\"webmaster@mad-kow.de\") -*-
")
)
(defun sk-comment ()
  "Add a comment to a C file"
  (interactive)
  (insert "/**
 * \\brief
 *
 */
")
  (search-backward "brief")
  (forward-char 6)
)

;; ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
(defun sk-python-header ()
  "Template for new python files"
  (interactive)
  ;; Insert header
  (insert "#!/usr/bin/env python

import sys
import os
sys.path.append(os.getenv('HOME') + '/bin/')

import general
import tools")
  ;; Mark python file executable
  (message "Making file executable (0744)")
  (set-file-modes buffer-file-name #o744)
  )

;; ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
(defun sk-shell-header ()
  "Template for new shell scripts"
  (interactive)
  (insert "#!/bin/bash

function error() {
	echo $1
	exit 1
}

function usage() {

    echo \"Usage: $0\"
    exit 1
}

\[\[ -n \"$1\" \]\] || usage

")
  (set-file-modes buffer-file-name #o744)
  )



;; ======================================================================
(defun sk-hg ()
  "Activate flyspell and auto-fill mode for hg commit files"
  (when (> (length (buffer-name)) 9)
    (when (string= (substring (buffer-name) 0 9) "hg-editor")
      (message "Buffer opened appears to be a hg commit")
      (flyspell-mode)
      (turn-on-auto-fill)
      )
    )
  )
(add-hook 'find-file-hook 'sk-hg)

;; http://stackoverflow.com/questions/143072/in-emacs-what-is-the-opposite-function-of-other-window-c-x-o
(defun back-window ()
  (interactive)
  (other-window -1))
(global-set-key (kbd "C-x O") 'back-window)

;; ======================================================================
;; Shortcuts
(global-set-key (kbd "M-C-r") 'replace-regexp)

;; http://stackoverflow.com/a/5826548
(global-set-key "\C-t" 'toggle-truncate-lines)

;; http://www.emacswiki.org/emacs/ToggleWindowSplit
(defun toggle-window-split ()
  (if (= (count-windows) 2)
      (let* ((this-win-buffer (window-buffer))
	     (next-win-buffer (window-buffer (next-window)))
	     (this-win-edges (window-edges (selected-window)))
	     (next-win-edges (window-edges (next-window)))
	     (this-win-2nd (not (and (<= (car this-win-edges)
					 (car next-win-edges))
				     (<= (cadr this-win-edges)
					 (cadr next-win-edges)))))
	     (splitter
	      (if (= (car this-win-edges)
		     (car (window-edges (next-window))))
		  'split-window-horizontally
		'split-window-vertically)))
	(delete-other-windows)
	(let ((first-win (selected-window)))
	  (funcall splitter)
	  (if this-win-2nd (other-window 1))
	  (set-window-buffer (selected-window) this-win-buffer)
	  (set-window-buffer (next-window) next-win-buffer)
	  (select-window first-win)
	  (if this-win-2nd (other-window 1))))))

;; http://www.emacswiki.org/emacs/CompileCommand
(defun recompile-quietly ()
  "Re-compile without changing the window configuration."
  (interactive)
  (save-window-excursion
    (recompile)))

(global-set-key (kbd "C-x |") 'toggle-window-split)

(defun sk-c-template ()
  "Generating C template"
  (interactive)
  (insert "#include <iostream>

using namespace std;

int main()
{
  std::cout << \"Hello World!\" << std::endl;
  return 0;
}")
  (set (make-local-variable 'compile-command)
                    ;; emulate make's .c.o implicit pattern rule, but with
                    ;; different defaults for the CC, CPPFLAGS, and CFLAGS
                    ;; variables:
                    ;; $(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $<
       (let ((file (file-name-nondirectory buffer-file-name)))
	 (format "g++ -o %s %s %s %s && ./%s"
		 (file-name-sans-extension file)
		 (or (getenv "CPPFLAGS") "")
		 (or (getenv "CFLAGS") " -Wall -g")
		 file
		 (file-name-sans-extension file))))
)

(defun sk-python-template ()
  "Generating Python template"
  (interactive)
  (insert "#!/usr/bin/env python

import sys
import os
sys.path.append(os.getenv('HOME') + '/bin/')

import general
import tools")
)
