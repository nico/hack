function pp(input : String) : String {
    return input;
}

function run_pp() {
    let input = document.getElementsByTagName('textarea')[0].value;
    let output = pp(input);
    alert(output);
}