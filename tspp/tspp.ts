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
        for (; ;) {
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
    // preprocessing-token:
    //   header-name
    //   identifier
    //   pp-number
    //   character-literal
    //   user-defined-character-literal
    //   string-literal
    //   user-defined-string-literal
    //   preprocessing-op-or-punc
    //   each non-white-space character that cannot be one of the above
    // Hmm pptokens are context-dependent ("Example: see the handling of <
    // within a #include preprocessing directive."), so probably have to mix phases
    // 4 and 3 here.

    // [cpp]
    // preprocessing-file:
    //   group_opt
    // group:
    //   group-part
    //   group group-part
    // group-part:
    //   if-section
    //   control-line
    //   text-line
    //   # non-directive
    // if-section:
    //   if-group elif-groups_opt else-group_opt endif-line
    // if-group:
    //   # if constant-expression new-line group_opt
    //   # ifdef identifier new-line group_opt
    //   # ifndef identifier new-line group_opt
    // elif-groups:
    //   elif-group
    //   elif-groups elif-group
    // elif-group:
    //   # elif constant-expression new-line group_opt
    // else-group:
    //   # else new-line group_opt
    // endif-line:
    //   # endif new-line
    // control-line:
    //   # include pp-tokens new-line
    //   # define identifier replacement-list new-line
    //   # define identifier lparen identifier-list_opt ) replacement-list new-line
    //   # define identifier lparen ... ) replacement-list new-line
    //   # define identifier lparen identifier-list, ... ) replacement-list new-line
    //   # undef identifier new-line
    //   # line pp-tokens new-line
    //   # error pp-tokens_opt new-line
    //   # pragma pp-tokens_opt new-line
    //   # new-line
    // text-line:
    //   pp-tokens_opt new-line
    // non-directive:
    //   pp-tokens new-line
    // lparen:
    //   a ( character not immediately preceded by white-space
    // identifier-list:
    //   identifier
    //   identifier-list , identifier
    // replacement-list:
    //   pp-tokens_opt
    // pp-tokens:
    //   preprocessing-token
    //   pp-tokens preprocessing-token
    // new-line:
    //   the new-line character

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
