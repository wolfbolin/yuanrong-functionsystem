/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Package colorprint Print with color
package colorprint

import (
	"io"
	"strings"
)

// PrintSuccess Print Success Statement
func PrintSuccess(w io.Writer, successWord, str string) {
	PrintColorWord(w, successWord, SuccessColor)
	if str != "" {
		PrintlnColorWord(w, str, Colorless)
	}
}

// PrintFail Print Failure Statement
func PrintFail(w io.Writer, str, errorWord, suffix string) {
	if str != "" {
		PrintColorWord(w, str, Colorless)
	}
	PrintColorWord(w, errorWord, ErrorColor)
	if suffix != "" {
		PrintColorWord(w, suffix, Colorless)
	}
}

// PrintInteractive Print interactive statements
func PrintInteractive(w io.Writer, interactiveWord string) {
	PrintColorWord(w, interactiveWord, InteractiveColor)
}

// PrintKeywords Print statements with keywords
func PrintKeywords(w io.Writer, printMap []StringColorInfo) {
	for _, keywordConfig := range printMap {
		PrintColorWord(w, keywordConfig.Str, keywordConfig.Color)
	}
}

// PrintColorWord Print a colored string to w
func PrintColorWord(w io.Writer, str string, color ColorType) {
	printColorString(w, str, color)
}

// PrintlnColorWord Print a colored string and \n to w
func PrintlnColorWord(w io.Writer, str string, color ColorType) {
	str = str + "\n"
	printColorString(w, str, color)
}

// GetPrintString Get colored string to compare
func GetPrintString(str string, color ColorType) string {
	return getPrintString(str, color)
}

// NewColorConfig create PrintStringConfig
func NewColorConfig() *PrintStringConfig {
	colorConfig := PrintStringConfig{
		ColorConfig: defaultColorConfig,
	}
	return &colorConfig
}

// AddColorConfig Add a Config Before Using PrintStringUsingColorConfig
func (p *PrintStringConfig) AddColorConfig(str string, color ColorType) *PrintStringConfig {
	if str == "" {
		return p
	}
	p.ColorConfig[str] = color
	return p
}

// DeleteColorConfig Delete a Config Before Using PrintStringUsingColorConfig
func (p *PrintStringConfig) DeleteColorConfig(str string) *PrintStringConfig {
	if str == "" {
		return p
	}
	delete(p.ColorConfig, str)
	return p
}

// GetPrintStringUsingMap Print string to w using color config
func (p *PrintStringConfig) GetPrintStringUsingMap(printMap []StringColorInfo) {
	for _, keywordConfig := range printMap {
		getPrintString(keywordConfig.Str, keywordConfig.Color)
	}
	return
}

// PrintStringUsingColorConfig Print string to w using color config
func (p *PrintStringConfig) PrintStringUsingColorConfig(w io.Writer, str string) {
	result := p.split(str)
	for _, printString := range result {
		color, ok := p.ColorConfig[printString]
		if ok {
			printColorString(w, printString, color)
			continue
		}
		printColorString(w, printString, Colorless)
	}
	return
}

// GetPrintStringUsingColorConfig Get print string to compare
func (p *PrintStringConfig) GetPrintStringUsingColorConfig(str string) string {
	result := p.split(str)
	var coloredString strings.Builder
	for _, printString := range result {
		color, ok := p.ColorConfig[printString]
		if ok {
			coloredString.WriteString(getPrintString(printString, color))
			continue
		}
		coloredString.WriteString(getPrintString(printString, Colorless))
	}
	return coloredString.String()
}

func (p *PrintStringConfig) split(str string) []string {
	ac := newAcAutoMachine()
	for acStr := range p.ColorConfig {
		ac.addPattern(acStr)
	}
	ac.buildTrie()
	strArr := strings.Split(str, "\n")
	var result []string
	for _, content := range strArr {
		result = append(result, ac.splitWithAC(content)...)
		result = append(result, "\n")
	}
	return result[:len(result)-1]
}
