<!--

This list can be auto generated with go-licenses

go install github.com/google/go-licenses@latest

From the RPK directory

go-licenses report ./... --template ../../../licenses/golang_deps.tpl

-->

# Go deps _used_ in production in RPK (exclude all test dependencies)

| software     | license        |
| :----------: | :------------: |
{{ range . -}}
| {{ .Name }} | [{{ .LicenseName }}]({{ .LicenseURL }}) |
{{ end }}

