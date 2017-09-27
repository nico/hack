function phase1(input: string): string {
    // Only trigraphs are interesting to us here.
    // FIXME: Show in UI if anything happens in this phase.
    return input
        .replace(/\?\?</g, '{')
        .replace(/\?\?>/g, '}')
        .replace(/\?\?\(/g, '[')
        .replace(/\?\?\)/g, ']')
        .replace(/\?\?=/g, '#')
        .replace(/\?\?\//g, '\\')
        .replace(/\?\?'/g, '^')
        .replace(/\?\?!/g, '|')
        .replace(/\?\?-/g, '~')
        ;

}

function phase2(input: string): string {
    return input.replace(/\\\n/g, '');
}

function pp(input: string): string {
    input = phase1(input);
    input = phase2(input);
    return input;
}

function run_pp() {
    let input = document.getElementsByTagName('textarea')[0].value;
    let output = pp(input);
    document.getElementById('output').innerText = output;
}
