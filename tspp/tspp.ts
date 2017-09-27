function pp(input : string) : string {
    return input;
}

function run_pp() {
    let input = document.getElementsByTagName('textarea')[0].value;
    let output = pp(input);
    document.getElementById('output').innerText = output;
}
