// [lex.phases]p1
function phase1(input: string): string {
    // Only trigraphs are interesting to us here [lex.trigraph].
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

// [lex.phases]p2
function phase2(input: string): string {
    return input.replace(/\\\n/g, '');
}

// [lex.phases]p3
// FIXME: PPToken type for return.
function phase3(input: string): [string] {
    // "Each comment is replaced by one space character."
    function remove_comments(input: string): string {
        let spanstart = 0, searchstart = 0;
        let output = '';
        for (;;) {
            let newpos = input.indexOf('/', searchstart);
            if (newpos == -1 || newpos == input.length - 1) {
                output += input.slice(spanstart);
                break;
            }
            if (input[newpos + 1] == '/') {
                // Skip single-line comment.
                output += input.slice(spanstart, newpos) + ' ';
                spanstart = input.indexOf('\n', newpos + 2);
                if (spanstart == -1)
                    break;
                spanstart++;  // Skip \n
                searchstart = spanstart;
                continue;
            }
            if (input[newpos + 1] == '*') {
                // Skip block comment.
                output += input.slice(spanstart, newpos) + ' ';
                spanstart = input.indexOf('*/', newpos + 2);
                if (spanstart == -1)
                    throw "Unterminated /* comment";
                spanstart += 2;  // Skip */
                searchstart = spanstart;
                continue;
            }
            // If we get here, it was just a /, not a comment.
            // Advance the search position, but not span start.
            searchstart = newpos + 1;
        }
        return output;
    }
    // [lex.pptoken]
    return [remove_comments(input)] as [string];
}

function pp(input: string): string {
    input = phase1(input);
    input = phase2(input);
    let pptokens = phase3(input);
    return pptokens.join();
}

function run_pp() {
    let input = document.getElementsByTagName('textarea')[0].value;
    let output = pp(input);
    document.getElementById('output').innerText = output;
}
